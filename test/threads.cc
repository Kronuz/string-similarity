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

// Metrics called from several threads at once: the mask scratch and the bigram table
// are per thread, so every thread must still get the oracle's answer, for pairs of
// every length class (one word, blocked, past the stack buffers). Run in the
// ThreadSanitizer job as well.
// Named check: threads.

#include <atomic>
#include <cstdio>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "jaro.h"
#include "jaro_winkler.h"
#include "levenshtein.h"
#include "oracle.h"
#include "sanitizer.h"
#include "sorensen_dice.h"

static std::atomic<size_t> checks{0};
static std::atomic<int> failures{0};

static void expect(double got, double want, const char* what, size_t la, size_t lb) {
	++checks;
	if (got != want) {
		if (++failures < 20) std::printf("FAIL %s: got %.17g want %.17g lens %zu/%zu\n", what, got, want, la, lb);
	}
}

static void worker(unsigned seed, int rounds) {
	std::mt19937_64 rng(seed);
	const size_t lens[] = {1, 2, 5, 12, 40, 63, 64, 65, 130, 300, 515};
	Levenshtein lev(false);
	Jaro jaro(false);
	Jaro_Winkler jw(false);
	Sorensen_Dice dice(false);
	for (int r = 0; r < rounds; ++r) {
		const size_t la = lens[rng() % std::size(lens)], lb = lens[rng() % std::size(lens)];
		const unsigned alpha = 1 + rng() % 12;
		std::string a(la, 'a'), b(lb, 'a');
		for (auto& c : a) c = char('a' + rng() % alpha);
		for (auto& c : b) c = char('a' + rng() % alpha);
		if (rng() % 2 && !a.empty()) {
			b = a;
			b[rng() % b.size()] = char('a' + rng() % alpha);
		}
		const size_t lbb = b.size();
		const bool same = a == b;
		expect(lev.distance(a, b), oracle::lev_distance(a, b, 1, 1), "threads", la, lbb);
		expect(jaro.similarity(a, b), same ? 1.0 : oracle::jaro(a, b).value, "threads", la, lbb);
		expect(jw.similarity(a, b), same ? 1.0 : oracle::jaro_winkler(a, b), "threads", la, lbb);
		const double d = a.size() < 2 || b.size() < 2 ? (same ? 1.0 : 0.0) : (same ? 1.0 : oracle::dice_multiset(a, b));
		expect(dice.similarity(a, b), d, "threads", la, lbb);
		Jaro bound(a, false);
		expect(bound.similarity(b), same ? 1.0 : oracle::jaro(a, b).value, "threads", la, lbb);
	}
}

int main() {
	const int rounds = STRSIM_SANITIZED ? 150 : 1500;
	std::vector<std::thread> threads;
	for (unsigned t = 0; t < 8; ++t) threads.emplace_back(worker, 1000 + t, rounds);
	for (auto& t : threads) t.join();
	std::printf("%zu checks, %d failures\n", checks.load(), failures.load());
	if (failures) return 1;
	std::printf("all thread checks passed\n");
	return 0;
}
