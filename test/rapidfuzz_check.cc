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

// Cross-check against rapidfuzz-cpp (a comparator: fetched for this test and the bench
// only, never a dependency of the library): Jaro and Jaro-Winkler on random pairs
// (one million, 40000 under a sanitizer) must agree to 1e-12, in both argument orders,
// case-sensitive and bound; unit-cost Levenshtein on short pairs and on long related
// pairs likewise. CTest string_similarity_rapidfuzz_check.
// Named check: rapidfuzz.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

#include <rapidfuzz/distance/Jaro.hpp>
#include <rapidfuzz/distance/JaroWinkler.hpp>
#include <rapidfuzz/distance/Levenshtein.hpp>

#include "jaro.h"
#include "jaro_winkler.h"
#include "levenshtein.h"
#include "sanitizer.h"

int main() {
	std::mt19937_64 rng(7);
	Jaro j(false);
	Jaro_Winkler w(false);
	size_t bad = 0;
	const size_t N = STRSIM_SANITIZED ? 40000 : 1000000;
	for (size_t n = 0; n < N; ++n) {
		const unsigned alpha = 1 + rng() % 30;
		const size_t maxlen = (n % 50 == 0) ? 400 : 40;
		std::string a(1 + rng() % maxlen, 'a'), b(1 + rng() % maxlen, 'a');
		for (auto& c : a) c = char('a' + rng() % alpha);
		if (rng() % 2) {  // related pair
			b = a;
			for (unsigned e = rng() % 6; e; --e) b[rng() % b.size()] = char('a' + rng() % alpha);
			if (rng() % 2) std::swap(b[rng() % b.size()], b[rng() % b.size()]);
		} else {
			for (auto& c : b) c = char('a' + rng() % alpha);
		}
		const double rj = rapidfuzz::jaro_similarity(a, b);
		const double rw = rapidfuzz::jaro_winkler_similarity(a, b);
		const double rjr = rapidfuzz::jaro_similarity(b, a);
		const double rwr = rapidfuzz::jaro_winkler_similarity(b, a);
		Jaro jb(a, false);
		Jaro_Winkler wb(a, false);
		const bool ok = std::fabs(j.similarity(a, b) - rj) <= 1e-12 && std::fabs(w.similarity(a, b) - rw) <= 1e-12 &&
			std::fabs(j.similarity(b, a) - rjr) <= 1e-12 && std::fabs(w.similarity(b, a) - rwr) <= 1e-12 &&
			std::fabs(jb.similarity(b) - rj) <= 1e-12 && std::fabs(wb.similarity(b) - rw) <= 1e-12;
		if (!ok) {
			if (++bad < 10) std::printf("DIFF jaro %.17g vs %.17g, jw %.17g vs %.17g a=%s b=%s\n", j.similarity(a, b), rj, w.similarity(a, b), rw, a.c_str(), b.c_str());
		}
	}
	std::printf("%zu pairs, %zu disagreements\n", N, bad);

	// Levenshtein (unit costs) against rapidfuzz: short pairs, and long related pairs
	// whose distance is small, where the banded kernel runs.
	Levenshtein lev(false);
	size_t lev_bad = 0;
	const size_t M = STRSIM_SANITIZED ? 60 : 600;
	for (size_t n = 0; n < M; ++n) {
		const unsigned alpha = 2 + rng() % 25;
		std::string a(1500 + rng() % 3000, 'a');
		for (auto& c : a) c = char('a' + rng() % alpha);
		std::string b = a;
		const size_t edits = 1 + rng() % (a.size() / (n % 3 == 0 ? 4 : 50));
		for (size_t e = 0; e < edits; ++e) {
			switch (rng() % 3) {
				case 0: b[rng() % b.size()] = char('a' + rng() % alpha); break;
				case 1: b.insert(b.begin() + rng() % (b.size() + 1), char('a' + rng() % alpha)); break;
				default: b.erase(b.begin() + rng() % b.size()); break;
			}
		}
		if (std::fabs(lev.distance(a, b) - rapidfuzz::levenshtein_normalized_distance(a, b)) > 1e-12) {
			if (++lev_bad < 10) std::printf("DIFF levenshtein lens %zu/%zu\n", a.size(), b.size());
		}
	}
	for (size_t n = 0; n < N / 10; ++n) {
		const unsigned alpha = 1 + rng() % 30;
		std::string a(1 + rng() % 200, 'a'), b(1 + rng() % 200, 'a');
		for (auto& c : a) c = char('a' + rng() % alpha);
		for (auto& c : b) c = char('a' + rng() % alpha);
		if (std::fabs(lev.distance(a, b) - rapidfuzz::levenshtein_normalized_distance(a, b)) > 1e-12) {
			if (++lev_bad < 10) std::printf("DIFF levenshtein lens %zu/%zu\n", a.size(), b.size());
		}
	}
	std::printf("levenshtein: %zu disagreements\n", lev_bad);
	return bad || lev_bad ? 1 : 0;
}
