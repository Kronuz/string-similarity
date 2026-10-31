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


// Differential test: every metric whose kernel was optimized must return exactly
// (bit for bit) what the frozen legacy implementation returns, EXCEPT for the
// input classes whose behavior was deliberately corrected (see README, "Behavior
// changes"). In those classes the legacy result is a known bug; the new result
// must equal the independent oracle (test/oracle.h) and the test asserts the two
// really differ somewhere (so the class list cannot silently go stale):
//   Levenshtein, insert/delete cost != 1: the left column was seeded with i + 1
//     instead of (i + 1) * cost.
//   Levenshtein, both costs 0: 0 / 0 (NaN) before, distance 0 now.
//   Jaro: repeated characters (the legacy two-pass matching is not the standard
//     one-pass), an odd number of out-of-order matches (legacy transpositions
//     were t / 2.0, the standard is integer t / 2), and two one-character strings
//     that are equal after case folding (legacy window wrapped around to empty).
//   Jaro_Winkler: legacy dispatched to plain Jaro, so every pair with a common
//     prefix and Jaro similarity above the boost threshold changes (plus the
//     Jaro classes above).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "levenshtein.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "legacy_metrics.h"
#include "oracle.h"

static std::mt19937_64 rng(0x5eed1234abcdULL);
static size_t checks = 0, failures = 0;
static size_t g_c0 = 0, g_c1 = 0;

static bool same(double a, double b) {
	return std::memcmp(&a, &b, sizeof a) == 0 || (a != a && b != b);
}

static void expect(double got, double want, const char* what, const std::string& a, const std::string& b, bool icase) {
	++checks;
	if (!same(got, want)) {
		if (++failures < 20) {
			std::printf("MISMATCH %s costs=%zu,%zu icase=%d got %.17g want %.17g lens %zu/%zu\n  a=\"%s\"\n  b=\"%s\"\n",
				what, g_c0, g_c1, icase, got, want, a.size(), b.size(),
				a.size() < 200 ? a.c_str() : "(long)", b.size() < 200 ? b.c_str() : "(long)");
		}
	}
}

static std::string random_string(size_t len, unsigned alphabet, unsigned base = 'a') {
	std::string s(len, '\0');
	for (auto& c : s) {
		c = static_cast<char>(base + rng() % alphabet);
	}
	return s;
}

// A mutated copy: substitutions, insertions, deletions, case flips.
static std::string mutate(std::string s, unsigned edits, unsigned alphabet, unsigned base) {
	for (unsigned k = 0; k < edits; ++k) {
		switch (rng() % 4) {
			case 0: if (!s.empty()) s[rng() % s.size()] = char(base + rng() % alphabet); break;
			case 1: s.insert(s.begin() + rng() % (s.size() + 1), char(base + rng() % alphabet)); break;
			case 2: if (!s.empty()) s.erase(s.begin() + rng() % s.size()); break;
			case 3: if (!s.empty()) { auto& c = s[rng() % s.size()]; c = (c >= 'a' && c <= 'z') ? char(c - 32) : (c >= 'A' && c <= 'Z') ? char(c + 32) : c; } break;
		}
	}
	return s;
}

static size_t g_changed[8] = {0};
enum { kLev, kLevBound, kJaro, kJaroBound, kJW, kJWBound };

// Folded text, as the library folds it (only the folding is shared; the metric is not).
static std::string fold(const std::string& s, bool icase) { return icase ? strings::upper(s) : s; }

// Jaro classes whose result is NOT corrected (new must equal legacy there).
static bool jaro_unchanged(const std::string& a, const std::string& b, bool icase) {
	const auto fa = fold(a, icase), fb = fold(b, icase);
	if (a.empty() || b.empty() || a == b) return true;  // base cases in StringMetric
	if (std::max(fa.size(), fb.size()) == 1) return false;
	for (const auto* s : {&fa, &fb}) {
		auto t = *s;
		std::sort(t.begin(), t.end());
		if (std::adjacent_find(t.begin(), t.end()) != t.end()) return false;
	}
	return oracle::jaro(fa, fb).mismatched % 2 == 0;
}

static double jw_prefix_boosts(const std::string& a, const std::string& b, bool icase) {
	const auto fa = fold(a, icase), fb = fold(b, icase);
	return fa.size() && fb.size() && fa[0] == fb[0] && oracle::jaro(fa, fb).value > 0.7;
}

// The O(n*m) Levenshtein oracle is skipped for long pairs (test/correctness.cc covers those).
static bool skip_oracle(const char* what, const std::string& x, const std::string& y) {
	return what[0] == 'L' && x.size() * y.size() > 20000;
}

// One metric against legacy (where unchanged) and the oracle (always).
//   want(x, y, dist): the oracle value for the pair, base cases included.
template <typename New, typename Old, typename Unchanged>
static void compare_changed(const New& n, const Old& o, const char* what, int cls, const std::string& a, const std::string& b, bool icase, Unchanged unchanged, double (*want)(const std::string&, const std::string&, bool, bool)) {
	for (int dir = 0; dir < 2; ++dir) {
		const auto& x = dir ? b : a;
		const auto& y = dir ? a : b;
		const bool same_as_legacy = unchanged(x, y);
		for (bool dist : {true, false}) {
			const double got = dist ? n.distance(x, y) : n.similarity(x, y);
			const double old = dist ? o.distance(x, y) : o.similarity(x, y);
			if (!skip_oracle(what, x, y)) expect(got, want(x, y, dist, icase), what, x, y, icase);
			if (same_as_legacy) {
				expect(got, old, what, x, y, icase);
			} else if (!same(got, old)) {
				++g_changed[cls];
			}
		}
		if (dir == 0 && a.size() * b.size() > 20000) break;
	}
}

template <typename New, typename Old, typename Unchanged>
static void compare_changed_bound(const New& n, const Old& o, const char* what, int cls, const std::string& a, const std::string& b, bool icase, Unchanged unchanged, double (*want)(const std::string&, const std::string&, bool, bool)) {
	// A bound metric stores its string already folded, and the base-case equality
	// test compares that folded copy with the raw argument.
	const std::string ao = fold(a, icase);
	const bool same_as_legacy = unchanged(ao, b);
	for (bool dist : {true, false}) {
		const double got = dist ? n.distance(b) : n.similarity(b);
		const double old = dist ? o.distance(b) : o.similarity(b);
		if (!skip_oracle(what, a, b)) expect(got, want(ao, b, dist, icase), what, a, b, icase);
		if (same_as_legacy) {
			expect(got, old, what, a, b, icase);
		} else if (!same(got, old)) {
			++g_changed[cls];
		}
	}
}

static double want_jaro(const std::string& a, const std::string& b, bool dist, bool icase) {
	double s;
	if (a.empty() || b.empty()) s = 0.0;
	else if (a == b) s = 1.0;
	else s = oracle::jaro(fold(a, icase), fold(b, icase)).value;
	return dist ? 1.0 - s : s;
}

static double want_jw(const std::string& a, const std::string& b, bool dist, bool icase) {
	double s;
	if (a.empty() || b.empty()) s = 0.0;
	else if (a == b) s = 1.0;
	else s = oracle::jaro_winkler(fold(a, icase), fold(b, icase));
	return dist ? 1.0 - s : s;
}

static double want_lev(const std::string& a, const std::string& b, bool dist, bool icase) {
	if (a.empty() || b.empty()) return dist ? 1.0 : 0.0;
	if (a == b) return dist ? 0.0 : 1.0;
	// Not equal before folding: the textbook formula (0 / 0 for zero costs).
	if (g_c0 == 0 && g_c1 == 0) return dist ? 0.0 : 1.0;  // everything is free
	const auto fa = fold(a, icase), fb = fold(b, icase);
	const double d = (double)oracle::lev_raw(fa, fb, g_c0, g_c1) / (std::max(g_c0, g_c1) * std::max(fa.size(), fb.size()));
	return dist ? d : 1.0 - d;
}

template <typename New, typename Old>
static void compare(const New& n, const Old& o, const char* what, const std::string& a, const std::string& b, bool icase) {
	expect(n.distance(a, b), o.distance(a, b), what, a, b, icase);
	if (a.size() * b.size() > 20000) return;  // the legacy kernel is O(n*m): fewer directions for long inputs
	expect(n.similarity(a, b), o.similarity(a, b), what, a, b, icase);
	expect(n.distance(b, a), o.distance(b, a), what, b, a, icase);
	expect(n.similarity(b, a), o.similarity(b, a), what, b, a, icase);
}

static void run_pair(const std::string& a, const std::string& b) {
	static const size_t costs[][2] = {{1, 1}, {2, 2}, {1, 2}, {2, 1}, {3, 1}, {1, 3}, {0, 1}, {1, 0}, {0, 0}};
	for (bool icase : {false, true}) {
		for (const auto& c : costs) {
			g_c0 = c[0]; g_c1 = c[1];
			Levenshtein nl(icase, c[0], c[1]);
			legacy::Levenshtein ol(icase, c[0], c[1]);
			const bool lev_same = c[1] == 1;  // insert/delete cost 1: unchanged
			compare_changed(nl, ol, "Levenshtein", kLev, a, b, icase, [&](const std::string&, const std::string&) { return lev_same; }, want_lev);
			Levenshtein nlb(a, icase, c[0], c[1]);
			legacy::Levenshtein olb(a, icase, c[0], c[1]);
			compare_changed_bound(nlb, olb, "Levenshtein(bound)", kLevBound, a, b, icase, [&](const std::string&, const std::string&) { return lev_same; }, want_lev);
			// Only the default costs for long inputs: the legacy kernel is O(n*m).
			if (a.size() * b.size() > 20000) break;
		}
		auto ju = [&](const std::string& x, const std::string& y) { return jaro_unchanged(x, y, icase); };
		auto wu = [&](const std::string& x, const std::string& y) { return jaro_unchanged(x, y, icase) && !jw_prefix_boosts(x, y, icase); };
		Jaro nj(icase);
		legacy::Jaro oj(icase);
		compare_changed(nj, oj, "Jaro", kJaro, a, b, icase, ju, want_jaro);
		Jaro njb(a, icase);
		legacy::Jaro ojb(a, icase);
		compare_changed_bound(njb, ojb, "Jaro(bound)", kJaroBound, a, b, icase, ju, want_jaro);
		Jaro_Winkler nw(icase);
		legacy::Jaro_Winkler ow(icase);
		compare_changed(nw, ow, "Jaro_Winkler", kJW, a, b, icase, wu, want_jw);
		Jaro_Winkler nwb(a, icase);
		legacy::Jaro_Winkler owb(a, icase);
		compare_changed_bound(nwb, owb, "Jaro_Winkler(bound)", kJWBound, a, b, icase, wu, want_jw);
	}
}

int main(int argc, char**) {
	const bool quick = argc > 1;  // any argument: a smaller run (for slow sanitizer builds)
	// Edge inputs.
	const std::vector<std::string> edges = {
		"", "a", "A", "b", "ab", "AB", "aB", "ba", "abc", "cba", "aaaa", "aaab", "abcdefgh", "ABCDEFGH",
		"kitten", "sitting", "MARTHA", "MARHTA", "DWAYNE", "DUANE", "DIXON", "DICKSONX",
		std::string("\xc3\xa9\xc3\xa8\xc3\xb1", 6), std::string("\xc3\x89\xc3\x88\xc3\x91", 6),
		std::string("a\0b", 3), std::string("a\0c", 3), std::string("\xff\xfe\xfd"), std::string("\xff\xfe\xfc"),
		std::string(64, 'x'), std::string(65, 'x'), std::string(63, 'x'), std::string(128, 'y'), std::string(129, 'y'),
		std::string(64, 'x') + "y", "y" + std::string(64, 'x'),
	};
	for (const auto& a : edges) {
		for (const auto& b : edges) {
			run_pair(a, b);
		}
	}

	// Random pairs across alphabets and lengths (word sized, around the 64/128 word
	// boundaries, long, and past the on-the-fly folding stack buffer).
	const size_t lens[] = {1, 2, 3, 5, 8, 12, 20, 33, 63, 64, 65, 66, 100, 127, 128, 129, 191, 192, 193, 255, 300, 513, 700};
	const size_t nlens = quick ? 17 : sizeof lens / sizeof *lens;
	const unsigned alphabets[] = {1, 2, 4, 26, 62, 200};
	for (unsigned alpha : alphabets) {
		const unsigned base = alpha > 62 ? 1 : 'A';  // mixes upper case / digits / symbols, and high bytes
		for (size_t ia = 0; ia < nlens; ++ia) {
			const size_t la = lens[ia];
			for (size_t ib = 0; ib < nlens; ++ib) {
				const size_t lb = lens[ib];
				const int reps = (la * lb > 100000) ? 1 : (la * lb > 5000 ? 2 : 10);
				for (int r = 0; r < reps; ++r) {
					run_pair(random_string(la, alpha, base), random_string(lb, alpha, base));
				}
			}
			// Related pairs: the interesting regime for transpositions and short distances.
			for (int r = 0; r < (la > 300 ? 1 : 6); ++r) {
				auto a = random_string(la, alpha, base);
				run_pair(a, mutate(a, 1 + rng() % 6, alpha, base));
				run_pair(a, mutate(a, la / 4 + 1, alpha, base));
			}
		}
	}
	// Mixed case words.
	for (int r = 0; r < (quick ? 300 : 3000); ++r) {
		auto a = random_string(1 + rng() % 14, 52, 'A');
		for (auto& c : a) if (c > 'Z' && c < 'a') c = char(c + 32);
		auto b = mutate(a, rng() % 4, 52, 'A');
		run_pair(a, b);
	}

	std::printf("%zu checks, %zu failures\n", checks, failures);
	static const char* names[] = {"Levenshtein", "Levenshtein(bound)", "Jaro", "Jaro(bound)", "Jaro_Winkler", "Jaro_Winkler(bound)"};
	for (int k = 0; k <= kJWBound; ++k) {
		std::printf("deliberate differences from legacy, %s: %zu\n", names[k], g_changed[k]);
		if (!g_changed[k]) {
			std::printf("MISMATCH: no pair exercised the corrected class for %s\n", names[k]);
			++failures;
		}
	}
	if (failures) return 1;
	std::printf("all differential checks passed\n");
	return 0;
}
