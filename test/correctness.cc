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
//   levenshtein_weighted, jaro, jaro_winkler, lcs, jaccard, dice, icase_len1, levenshtein_zero, phonetic.

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
	// PhoneticMetric: encode both operands, then compare the codes.
	{
		SoundexEnglish enc;
		PhoneticMetric<SoundexEnglish, Levenshtein> pm;
		const char* words[] = {"Robert", "Rupert", "Rubin", "Ashcraft", "Tymczak", "Pfister", "Honeyman", "Lee", "x", "Washington", "Wasington"};
		for (const char* x : words) {
			for (const char* y : words) {
				const std::string a(x), b(y), ea = enc.encode(a), eb = enc.encode(b);
				const double want = ea == eb ? 0.0 : oracle::lev_distance(ea, eb, 1, 1);
				expect(pm.distance(a, b), want, "phonetic", a, b);
				expect(pm.similarity(a, b), 1.0 - want, "phonetic", a, b);
				PhoneticMetric<SoundexEnglish, Levenshtein> bound(a);
				expect(bound.distance(b), want, "phonetic", a, b);
				expect(bound.similarity(b), 1.0 - want, "phonetic", a, b);
			}
		}
		expect(pm.similarity(std::string("Robert"), std::string("Rupert")), 1.0, "phonetic", "Robert", "Rupert");
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

	std::printf("%zu checks\n", checks);
	if (!failed.empty()) {
		std::printf("FAILED: %s\n", failed.c_str());
		return 1;
	}
	std::printf("all correctness checks passed\n");
	return 0;
}
