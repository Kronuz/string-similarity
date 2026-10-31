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

// Cross-check against rapidfuzz-cpp (a comparator: fetched by the bench option
// only, never a dependency of the library): Jaro and Jaro-Winkler on one million
// random pairs must agree to 1e-12.

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

#include <rapidfuzz/distance/Jaro.hpp>
#include <rapidfuzz/distance/JaroWinkler.hpp>

#include "jaro.h"
#include "jaro_winkler.h"

int main() {
	std::mt19937_64 rng(7);
	Jaro j(false);
	Jaro_Winkler w(false);
	size_t bad = 0;
	const size_t N = 1000000;
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
		if (std::fabs(j.similarity(a, b) - rj) > 1e-12 || std::fabs(w.similarity(a, b) - rw) > 1e-12) {
			if (++bad < 10) std::printf("DIFF jaro %.17g vs %.17g, jw %.17g vs %.17g a=%s b=%s\n", j.similarity(a, b), rj, w.similarity(a, b), rw, a.c_str(), b.c_str());
		}
	}
	std::printf("%zu pairs, %zu disagreements\n", N, bad);
	return bad ? 1 : 0;
}
