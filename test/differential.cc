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
// (bit for bit) what the frozen legacy implementation returns.

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

template <typename New, typename Old>
static void compare(const New& n, const Old& o, const char* what, const std::string& a, const std::string& b, bool icase) {
	expect(n.distance(a, b), o.distance(a, b), what, a, b, icase);
	if (a.size() * b.size() > 20000) return;  // the legacy kernel is O(n*m): fewer directions for long inputs
	expect(n.similarity(a, b), o.similarity(a, b), what, a, b, icase);
	expect(n.distance(b, a), o.distance(b, a), what, b, a, icase);
	expect(n.similarity(b, a), o.similarity(b, a), what, b, a, icase);
}

template <typename New, typename Old>
static void compare_bound(const New& n, const Old& o, const char* what, const std::string& a, const std::string& b, bool icase) {
	expect(n.distance(b), o.distance(b), what, a, b, icase);
	expect(n.similarity(b), o.similarity(b), what, a, b, icase);
}

static void run_pair(const std::string& a, const std::string& b) {
	static const size_t costs[][2] = {{1, 1}, {2, 2}, {1, 2}, {2, 1}, {3, 1}, {1, 3}, {0, 1}, {1, 0}, {0, 0}};
	for (bool icase : {false, true}) {
		for (const auto& c : costs) {
			g_c0 = c[0]; g_c1 = c[1];
			Levenshtein nl(icase, c[0], c[1]);
			legacy::Levenshtein ol(icase, c[0], c[1]);
			compare(nl, ol, "Levenshtein", a, b, icase);
			Levenshtein nlb(a, icase, c[0], c[1]);
			legacy::Levenshtein olb(a, icase, c[0], c[1]);
			compare_bound(nlb, olb, "Levenshtein(bound)", a, b, icase);
			// Only the default costs for long inputs: the legacy kernel is O(n*m).
			if (a.size() * b.size() > 20000) break;
		}
		Jaro nj(icase);
		legacy::Jaro oj(icase);
		compare(nj, oj, "Jaro", a, b, icase);
		Jaro njb(a, icase);
		legacy::Jaro ojb(a, icase);
		compare_bound(njb, ojb, "Jaro(bound)", a, b, icase);
		Jaro_Winkler nw(icase);
		legacy::Jaro_Winkler ow(icase);
		compare(nw, ow, "Jaro_Winkler", a, b, icase);
		Jaro_Winkler nwb(a, icase);
		legacy::Jaro_Winkler owb(a, icase);
		compare_bound(nwb, owb, "Jaro_Winkler(bound)", a, b, icase);
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
	if (failures) return 1;
	std::printf("all differential checks passed\n");
	return 0;
}
