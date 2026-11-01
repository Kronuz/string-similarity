/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// Correctness tests: every metric is checked against an independent oracle
// (test/oracle.h: textbook definitions). Exhaustive over small alphabets and
// lengths, plus random long strings that cross the 64-bit word boundaries.
// Named checks (so a reverted fix fails a named test):
//   levenshtein_weighted, jaro, jaro_winkler, lcs, jaccard, dice, icase_len1, levenshtein_zero,
//   phonetic (and phonetic_jaro, phonetic_jaro_winkler, phonetic_dice: every inner metric, both forms),
//   icase_<metric> (icase=true equals fold-then-compare), icase_dice, jw_boundary (jd == bt).

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "jaccard.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "lcsubsequence.h"
#include "lcsubstr.h"
#include "levenshtein.h"
#include "oracle.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"
#include "english_soundex.h"

static size_t checks = 0;
static std::string failed;

static void expect(double got, double want, const char* test, const std::string& a, const std::string& b) {
	++checks;
	if (!(got == want || (got != got && want != want))) {
		if (failed.find(test) == std::string::npos) {
			failed += test;
			failed += ' ';
		}
		static int shown = 0;
		if (++shown < 20) {
			std::printf("FAIL %s: got %.17g want %.17g a=\"%s\" b=\"%s\"\n", test, got, want, a.c_str(), b.c_str());
		}
	}
}

static std::vector<std::string> all_strings(const std::string& alphabet, size_t max_len) {
	std::vector<std::string> out{""};
	size_t begin = 0;
	for (size_t len = 1; len <= max_len; ++len) {
		const size_t end = out.size();
		for (size_t i = begin; i < end; ++i)
			for (char c : alphabet) out.push_back(out[i] + c);
		begin = end;
	}
	return out;
}

static std::mt19937_64 rng(42);

static std::string rnd(size_t len, unsigned alpha) {
	std::string s(len, 'a');
	for (auto& c : s) c = char('a' + rng() % alpha);
	return s;
}

// A random string of any bytes drawn from `alpha` consecutive byte values starting at `base`.
static std::string rnd_bytes(size_t len, unsigned alpha, unsigned base) {
	std::string s(len, '\0');
	for (auto& c : s) c = char(base + rng() % alpha);
	return s;
}

// The library's convention around a textbook value: an empty operand scores 0 and
// identical operands 1, before any metric runs.
static double with_base_cases(const std::string& a, const std::string& b, double textbook) {
	if (a.empty() || b.empty()) return 0.0;
	if (a == b) return 1.0;
	return textbook;
}

// PhoneticMetric<SoundexEnglish, Inner>: the code of each operand, then the inner
// metric over the codes (`want` is the oracle's similarity of two different codes).
template <typename Inner, typename Want>
static void phonetic_check(const char* test, Want want) {
	SoundexEnglish enc;
	PhoneticMetric<SoundexEnglish, Inner> pm;
	const char* words[] = {"Robert", "Rupert", "Rubin", "Ashcraft", "Tymczak", "Pfister", "Honeyman", "Lee", "x", "Washington", "Wasington"};
	for (const char* x : words) {
		for (const char* y : words) {
			const std::string a(x), b(y), ea = enc.encode(a), eb = enc.encode(b);
			const double sim = with_base_cases(ea, eb, want(ea, eb));
			expect(pm.similarity(a, b), sim, test, a, b);
			expect(pm.distance(a, b), 1.0 - sim, test, a, b);
			PhoneticMetric<SoundexEnglish, Inner> bound(a);
			expect(bound.similarity(b), sim, test, a, b);
			expect(bound.distance(b), 1.0 - sim, test, a, b);
		}
	}
}

// icase=true means: fold both operands to upper case, then compare case-sensitively.
// The oracle is that equivalence itself, over every string up to length 4 on {a, A, b},
// plus high bytes, for the two-argument and the bound forms.
template <typename Metric>
static void icase_equivalence(const char* test) {
	std::vector<std::string> strs = all_strings("aAb", 4);
	strs.push_back("\xe9");
	strs.push_back("\xc9");
	strs.push_back("a\xe9");
	strs.push_back("A\xc9");
	Metric ci(true), cs(false);
	for (const auto& a : strs) {
		const std::string fa = strings::upper(a);
		Metric cib(a, true), csb(fa, false);
		for (const auto& b : strs) {
			const std::string fb = strings::upper(b);
			expect(ci.similarity(a, b), cs.similarity(fa, fb), test, a, b);
			expect(ci.distance(a, b), cs.distance(fa, fb), test, a, b);
			expect(cib.similarity(b), csb.similarity(fb), test, a, b);
			expect(cib.distance(b), csb.distance(fb), test, a, b);
		}
	}
}

// Jaro-Winkler boosts when jd > bt: nothing at jd == bt, and the boost one ulp below.
static void jaro_winkler_boundary(const std::string& a, const std::string& b) {
	const double jd = Jaro::jaro(a, b);
	expect(jd, oracle::jaro(a, b).value, "jw_boundary", a, b);
	for (double bt : {jd, std::nextafter(jd, 0.0), std::nextafter(jd, 1.0)}) {
		if (bt < 0.0 || bt > 1.0) continue;
		const double want = oracle::jaro_winkler(a, b, 0.1, bt);
		Jaro_Winkler w(false, 0.1, bt), wb(a, false, 0.1, bt);
		expect(w.similarity(a, b), want, "jw_boundary", a, b);
		expect(w.distance(a, b), 1.0 - want, "jw_boundary", a, b);
		expect(wb.similarity(b), want, "jw_boundary", a, b);
		expect(wb.distance(b), 1.0 - want, "jw_boundary", a, b);
	}
}

int main() {
	// Levenshtein, all cost pairs (including the unit-cost bit-parallel path),
	// exhaustive over {a,b,c} up to length 6.
	{
		const size_t costs[][2] = {{0, 0}, {1, 1}, {2, 2}, {1, 2}, {2, 1}, {3, 1}, {1, 3}, {5, 2}, {2, 5}, {1, 5}};
		const auto strs = all_strings("abc", 6);
		for (const auto& c : costs) {
			Levenshtein l(false, c[0], c[1]);
			for (size_t i = 0; i < strs.size(); ++i) {
				for (const auto& b : strs) {
					expect(l.distance(strs[i], b), oracle::lev_distance(strs[i], b, c[0], c[1]), "levenshtein_weighted", strs[i], b);
				}
			}
		}
		// Random, long (multi-word unit-cost path; weighted path with long columns).
		for (int r = 0; r < 300; ++r) {
			const auto a = rnd(1 + rng() % 200, 2 + rng() % 4), b = rnd(1 + rng() % 200, 2 + rng() % 4);
			for (const auto& c : costs) {
				Levenshtein l(false, c[0], c[1]);
				expect(l.distance(a, b), oracle::lev_distance(a, b, c[0], c[1]), "levenshtein_weighted", a, b);
			}
		}
	}

	// Jaro and Jaro-Winkler: exhaustive {a,b} to length 9 and {a,b,c} to length 6.
	{
		Jaro j(false);
		Jaro_Winkler w(false);
		Jaro_Winkler w2(false, 0.25, 0.0);
		auto run = [&](const std::string& a, const std::string& b) {
			if (a.empty() || b.empty() || a == b) return;
			expect(j.similarity(a, b), oracle::jaro(a, b).value, "jaro", a, b);
			expect(j.distance(a, b), 1.0 - oracle::jaro(a, b).value, "jaro", a, b);
			expect(w.similarity(a, b), oracle::jaro_winkler(a, b), "jaro_winkler", a, b);
			expect(w.distance(a, b), 1.0 - oracle::jaro_winkler(a, b), "jaro_winkler", a, b);
			expect(w2.similarity(a, b), oracle::jaro_winkler(a, b, 0.25, 0.0), "jaro_winkler", a, b);
			Jaro jb(a, false);
			Jaro_Winkler wb(a, false);
			expect(jb.similarity(b), oracle::jaro(a, b).value, "jaro", a, b);
			expect(wb.similarity(b), oracle::jaro_winkler(a, b), "jaro_winkler", a, b);
		};
		for (const auto& cfg : {std::pair<std::string, size_t>{"ab", 9}, {"abc", 6}}) {
			const auto strs = all_strings(cfg.first, cfg.second);
			for (const auto& a : strs)
				for (const auto& b : strs) run(a, b);
		}
		// Random, including lengths across 64/128 boundaries and unequal lengths.
		const size_t lens[] = {1, 2, 3, 10, 40, 63, 64, 65, 100, 127, 128, 129, 200, 300};
		for (size_t la : lens)
			for (size_t lb : lens)
				for (int r = 0; r < 40; ++r) {
					const unsigned alpha = 1 + rng() % 8;
					const auto a = rnd(la, alpha);
					run(a, rnd(lb, alpha));
					std::string b = a;
					for (int e = 0; e < 1 + int(rng() % 6); ++e) b[rng() % b.size()] = char('a' + rng() % alpha);
					run(a, b);
				}
		// Well-known values.
		expect(Jaro(false).similarity(std::string("MARTHA"), std::string("MARHTA")), (1.0 + 1.0 + 5.0 / 6.0) / 3.0, "jaro", "MARTHA", "MARHTA");
		Jaro_Winkler wk(false);
		const double mw = wk.similarity(std::string("MARTHA"), std::string("MARHTA"));
		++checks;
		if (std::fabs(mw - 0.9611111111111111) > 1e-12) { failed += "jaro_winkler "; std::printf("FAIL MARTHA/MARHTA JW %.17g\n", mw); }
		// name() must say Jaro_Winkler, and the public API must reach the boost.
		++checks;
		if (wk.name() != "Jaro_Winkler" || !(mw > Jaro(false).similarity(std::string("MARTHA"), std::string("MARHTA")))) { failed += "jaro_winkler "; std::printf("FAIL JW dispatch\n"); }
	}

	// Case-folded length-1 strings match (window 0): "a" vs "A" is 1.0 with icase.
	expect(Jaro(true).similarity(std::string("a"), std::string("A")), 1.0, "icase_len1", "a", "A");
	expect(Jaro_Winkler(true).similarity(std::string("a"), std::string("A")), 1.0, "icase_len1", "a", "A");
	expect(Jaro(true).distance(std::string("a"), std::string("A")), 0.0, "icase_len1", "a", "A");

	// Sorensen-Dice counts repeated bigrams: "aaaa" has 3 "aa" bigrams.
	expect(Sorensen_Dice(false).similarity(std::string("aaaa"), std::string("aaab")), 2.0 * 2 / 6, "dice", "aaaa", "aaab");
	// Both costs 0: every edit is free, distance 0 (similarity 1), never NaN.
	{
		Levenshtein z(false, 0, 0);
		expect(z.distance(std::string("abc"), std::string("xy")), 0.0, "levenshtein_zero", "abc", "xy");
		expect(z.similarity(std::string("abc"), std::string("xy")), 1.0, "levenshtein_zero", "abc", "xy");
		Levenshtein zb(std::string("abc"), false, 0, 0);
		expect(zb.distance(std::string("xy")), 0.0, "levenshtein_zero", "abc", "xy");
	}
	// PhoneticMetric: encode both operands, then compare the codes with the inner
	// metric, for every inner metric, in the two-argument and the bound forms.
	{
		phonetic_check<Levenshtein>("phonetic", [](const std::string& a, const std::string& b) { return 1.0 - oracle::lev_distance(a, b, 1, 1); });
		phonetic_check<Jaro>("phonetic_jaro", [](const std::string& a, const std::string& b) { return oracle::jaro(a, b).value; });
		phonetic_check<Jaro_Winkler>("phonetic_jaro_winkler", [](const std::string& a, const std::string& b) { return oracle::jaro_winkler(a, b); });
		phonetic_check<Sorensen_Dice>("phonetic_dice", [](const std::string& a, const std::string& b) { return oracle::dice_multiset(a, b); });
		PhoneticMetric<SoundexEnglish, Levenshtein> pm;
		expect(pm.similarity(std::string("Robert"), std::string("Rupert")), 1.0, "phonetic", "Robert", "Rupert");
		// The bound Dice case that used to score 0: R901096 and R90108 share 4 of 11 bigram slots.
		PhoneticMetric<SoundexEnglish, Sorensen_Dice> pd("Robert");
		expect(pd.similarity(std::string("Rubin")), 2.0 * 4 / 11, "phonetic_dice", "Robert", "Rubin");
	}

	// Other algorithms against their oracles (exhaustive {a,b,c} to length 5).
	{
		LCSubsequence ls(false);
		LCSubstr lt(false);
		Jaccard jc(false);
		Sorensen_Dice dc(false);
		const auto strs = all_strings("abc", 5);
		for (const auto& a : strs) {
			for (const auto& b : strs) {
				if (a.empty() || b.empty() || a == b) continue;
				const double mx = std::max(a.size(), b.size());
				expect(ls.similarity(a, b), oracle::lcs_subsequence(a, b) / mx, "lcs", a, b);
				expect(lt.similarity(a, b), oracle::lcs_substring(a, b) / mx, "lcs", a, b);
				expect(jc.similarity(a, b), oracle::jaccard(a, b), "jaccard", a, b);
				expect(dc.similarity(a, b), oracle::dice_multiset(a, b), "dice", a, b);
			}
		}
	}

	// Sorensen-Dice over bigram multisets: two-argument and bound forms, similarity and
	// distance, every string up to length 6 on {a, b, 0xff}, then random strings of
	// every length that picks a different path (2, 63..65, 129, 300, 3000) over
	// alphabets from one symbol to all 256 byte values. The calls share one table per
	// thread: a call that left a count behind would break the next oracle match.
	{
		Sorensen_Dice dc(false);
		auto run = [&](const std::string& a, const std::string& b) {
			const double want = with_base_cases(a, b, oracle::dice_multiset(a, b));
			Sorensen_Dice bound(a, false);
			expect(dc.similarity(a, b), want, "dice", a, b);
			expect(dc.distance(a, b), 1.0 - want, "dice", a, b);
			expect(bound.similarity(b), want, "dice", a, b);
			expect(bound.distance(b), 1.0 - want, "dice", a, b);
		};
		const auto strs = all_strings(std::string("ab\xff"), 6);
		for (const auto& a : strs)
			for (const auto& b : strs) run(a, b);
		const size_t lens[] = {2, 3, 5, 16, 63, 64, 65, 129, 300, 1000, 3000};
		const unsigned alphas[] = {1, 2, 4, 26, 256};
		for (unsigned alpha : alphas)
			for (size_t la : lens)
				for (size_t lb : lens)
					for (int r = 0; r < 2; ++r) {
						const auto a = rnd_bytes(la, alpha, alpha == 256 ? 0 : 'a');
						run(a, rnd_bytes(lb, alpha, alpha == 256 ? 0 : 'a'));
						auto b = a;
						for (int e = 0; e < 3; ++e) b[rng() % b.size()] = char(rng());
						run(a, b);
					}
		// One-character strings: identical ones score 1 like any identical pair.
		expect(Sorensen_Dice(false).similarity(std::string("a"), std::string("a")), 1.0, "dice", "a", "a");
		expect(Sorensen_Dice(false).similarity(std::string("a"), std::string("b")), 0.0, "dice", "a", "b");
		expect(Sorensen_Dice(false).similarity(std::string("a"), std::string("ab")), 0.0, "dice", "a", "ab");
		expect(Sorensen_Dice(true).similarity(std::string("a"), std::string("A")), 1.0, "icase_dice", "a", "A");
		expect(Sorensen_Dice(true).distance(std::string("a"), std::string("A")), 0.0, "icase_dice", "a", "A");
		expect(Sorensen_Dice(std::string("a"), true).similarity(std::string("a")), 1.0, "icase_dice", "a", "a");
		expect(Sorensen_Dice(std::string("a"), true).similarity(std::string("A")), 1.0, "icase_dice", "a", "A");
	}

	// icase=true is "fold both operands, then compare case-sensitively", for every metric.
	icase_equivalence<Levenshtein>("icase_levenshtein");
	icase_equivalence<Jaro>("icase_jaro");
	icase_equivalence<Jaro_Winkler>("icase_jaro_winkler");
	icase_equivalence<Sorensen_Dice>("icase_dice");
	icase_equivalence<LCSubsequence>("icase_lcs");
	icase_equivalence<LCSubstr>("icase_lcs");
	icase_equivalence<Jaccard>("icase_jaccard");

	// Jaro-Winkler at the boost threshold, on the pairs with a shared prefix whose Jaro
	// score is the threshold itself, and on random related pairs.
	{
		jaro_winkler_boundary("MARTHA", "MARHTA");
		jaro_winkler_boundary("DWAYNE", "DUANE");
		jaro_winkler_boundary("DIXON", "DICKSONX");
		for (int r = 0; r < 300; ++r) {
			const unsigned alpha = 2 + rng() % 8;
			auto a = rnd(5 + rng() % 70, alpha);
			auto b = a;
			for (int e = 0; e < 1 + int(rng() % 4); ++e) b[1 + rng() % (b.size() - 1)] = char('a' + rng() % alpha);
			if (a != b) jaro_winkler_boundary(a, b);
		}
	}

	// The sizes where an internal buffer changes hands: the weighted Levenshtein column
	// (256 on the stack), the case-folding buffer (512) and the Jaro match buffer (512).
	{
		for (size_t len = 254; len <= 259; ++len) {
			for (const auto& c : {std::pair<size_t, size_t>{2, 1}, {1, 3}}) {
				const auto a = rnd(len, 4), b = rnd(len + rng() % 3, 4);
				for (const auto& p : {std::pair<const std::string&, const std::string&>{a, b}, {b, a}}) {
					expect(Levenshtein(false, c.first, c.second).distance(p.first, p.second), oracle::lev_distance(p.first, p.second, c.first, c.second), "levenshtein_weighted", p.first, p.second);
				}
			}
		}
		for (size_t len = 509; len <= 515; ++len) {
			std::string a = rnd(len, 3), b = a;
			for (int e = 0; e < 6; ++e) b[rng() % b.size()] = char('a' + rng() % 3);
			for (auto& ch : a) if (rng() % 2) ch = char(ch - 32);
			for (auto& ch : b) if (rng() % 2) ch = char(ch - 32);
			const std::string fa = strings::upper(a), fb = strings::upper(b);
			for (const auto& p : {std::pair<const std::string&, const std::string&>{a, b}, {b, a}}) {
				const std::string &x = p.first, &y = p.second, fx = strings::upper(x), fy = strings::upper(y);
				expect(Jaro(true).similarity(x, y), with_base_cases(x, y, oracle::jaro(fx, fy).value), "jaro", x, y);
				expect(Jaro_Winkler(true).similarity(x, y), with_base_cases(x, y, oracle::jaro_winkler(fx, fy)), "jaro_winkler", x, y);
				expect(Jaro(false).similarity(fx, fy), with_base_cases(fx, fy, oracle::jaro(fx, fy).value), "jaro", fx, fy);
				expect(Sorensen_Dice(true).similarity(x, y), with_base_cases(x, y, oracle::dice_multiset(fx, fy)), "dice", x, y);
				expect(Levenshtein(true).distance(x, y), oracle::lev_distance(fx, fy, 1, 1), "levenshtein_weighted", x, y);
			}
		}
	}

	if (!failed.empty()) {
		std::printf("FAILED: %s\n", failed.c_str());
		return 1;
	}
	std::printf("all correctness checks passed\n");
	return 0;
}
