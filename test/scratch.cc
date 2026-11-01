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

// The per-thread mask scratch is capped. A thread keeps at most
// strsim_detail::kMaxScratchWords words (1 MiB by default) between calls, and a
// call that needs more takes a block of its own and frees it: results do not
// change and nothing stays behind.
//
// The same source builds twice: with the default cap (CTest string_similarity_scratch;
// strings of 40000 bytes of all 256 values need about 1.3 MiB of masks) and with a cap
// of 256 words (string_similarity_scratch_small, -DSTRSIM_MAX_SCRATCH_WORDS=256), where
// every string of a few hundred characters takes the one-off block. Results are checked
// against the oracles in test/oracle.h and the retained scratch against the cap after
// every call.
// Named checks: scratch_cap, scratch_jaro, scratch_levenshtein.

#include <algorithm>
#include <cstdio>
#include <initializer_list>
#include <random>
#include <string>

#include "jaro.h"
#include "levenshtein.h"
#include "oracle.h"

static std::mt19937_64 rng(2024);
static size_t checks = 0;
static int failures = 0;

static void expect(bool ok, const char* what, const std::string& detail) {
	++checks;
	if (!ok) {
		if (++failures < 30) std::printf("FAIL %s: %s\n", what, detail.c_str());
	}
}

static void expect_double(double got, double want, const char* what, size_t la, size_t lb, unsigned alpha) {
	expect(got == want, what, "got " + std::to_string(got) + " want " + std::to_string(want) + " lens " + std::to_string(la) + "/" + std::to_string(lb) + " alphabet " + std::to_string(alpha));
}

static std::string random_string(size_t len, unsigned alpha) {
	std::string s(len, '\0');
	for (auto& c : s) c = char(alpha == 256 ? rng() % 256 : 'a' + rng() % alpha);
	return s;
}

static void check_retained(const char* where, size_t len) {
	const size_t kept = strsim_detail::Peq<0>::retained_words();
	expect(kept <= strsim_detail::kMaxScratchWords, "scratch_cap", std::string(where) + ": kept " + std::to_string(kept) + " words after length " + std::to_string(len) + ", cap " + std::to_string(strsim_detail::kMaxScratchWords));
}

int main() {
	std::printf("scratch cap: %zu words\n", strsim_detail::kMaxScratchWords);

	// Sizes around the cap: Jaro against the oracle and Levenshtein against the
	// textbook dynamic program, over the alphabets whose mask tables differ most.
	const size_t lens[] = {65, 100, 130, 300, 700, 1200};
	const unsigned alphas[] = {2, 4, 26, 256};
	for (unsigned alpha : alphas) {
		for (size_t la : lens) {
			for (size_t lb : lens) {
				const auto a = random_string(la, alpha);
				auto related = a;
				related.resize(lb, 'x');
				for (int e = 0; e < 4; ++e) related[rng() % related.size()] = char(rng());
				const auto unrelated = random_string(lb, alpha);
				for (const std::string* y : std::initializer_list<const std::string*>{&related, &unrelated}) {
					expect_double(Jaro(false).similarity(a, *y), a == *y ? 1.0 : oracle::jaro(a, *y).value, "scratch_jaro", la, lb, alpha);
					check_retained("jaro", std::max(la, lb));
					expect_double(Levenshtein(false).distance(a, *y), oracle::lev_distance(a, *y, 1, 1), "scratch_levenshtein", la, lb, alpha);
					check_retained("levenshtein", std::max(la, lb));
				}
			}
		}
	}

	// A very long string of all 256 byte values: its masks do not fit the default cap.
	// Jaro against the oracle; Levenshtein where the distance is known exactly: a block
	// of 20 characters removed from a random string is at distance 20 (the length
	// difference is a lower bound and the removal an upper bound).
	{
		const size_t n = 40000;
		const auto a = random_string(n, 256), b = random_string(n, 256);
		expect_double(Jaro(false).similarity(a, b), oracle::jaro(a, b).value, "scratch_jaro", n, n, 256);
		check_retained("jaro 40000", n);
		std::string erased = a;
		erased.erase(15000, 20);
		const std::string c = erased;
		expect_double(Levenshtein(false).distance(a, c), 20.0 / double(n), "scratch_levenshtein", n, n - 20, 256);
		check_retained("levenshtein 40000", n);
		std::string changed = a;
		for (size_t pos : {1000u, 9000u, 17000u, 25000u, 33000u}) changed[pos] = char(changed[pos] + 1);
		const std::string d = changed;
		expect_double(Levenshtein(false).distance(a, d), 5.0 / double(n), "scratch_levenshtein", n, n, 256);
		check_retained("levenshtein 40000, 5 substitutions", n);
		// Small strings after the big ones: the index table was cleared.
		for (int r = 0; r < 50; ++r) {
			const auto x = random_string(70 + rng() % 200, 26), y = random_string(70 + rng() % 200, 26);
			expect_double(Jaro(false).similarity(x, y), oracle::jaro(x, y).value, "scratch_jaro", x.size(), y.size(), 26);
		}
	}

	std::printf("%zu checks, %d failures\n", checks, failures);
	if (failures) return 1;
	std::printf("all scratch checks passed\n");
	return 0;
}
