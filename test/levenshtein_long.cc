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


// Long-input test of the banded, multi-column Levenshtein kernel. Every distance
// must equal, exactly,
//   * a textbook two-row dynamic program (written here, sharing no code),
//   * the frozen previous kernel (test/frozen_levenshtein.h, the full-table
//     block kernel the banded one replaced),
// across lengths around the 64-bit word edges (63/64/65, 127/128/129) and long
// (1000, 3000, 10000), on random strings of several alphabets, near-identical
// strings (substitutions, insertions and deletions), strings that force the
// alignment far from the diagonal, and different lengths. It also drives the
// band directly: for every distance bound k a banded run is either exact (when
// the distance is at most k) or an upper bound above k, and the retry sized by
// a failed run must land on the exact distance whatever the first band was.
// Any argument, or a build under AddressSanitizer or ThreadSanitizer: a smaller run (no
// 10000-character pairs, fewer random rounds).

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "levenshtein.h"
#include "frozen_levenshtein.h"
#include "oracle.h"

// Under AddressSanitizer or ThreadSanitizer (about 3-10x slower) the smaller run is used.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LEVENSHTEIN_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define LEVENSHTEIN_TEST_SANITIZED 1
#endif
#endif
#ifndef LEVENSHTEIN_TEST_SANITIZED
#define LEVENSHTEIN_TEST_SANITIZED 0
#endif

static std::mt19937_64 rng(0x1e5e7a11ULL);
static size_t checks = 0, failures = 0;

static void fail(const char* what, const std::string& a, const std::string& b, size_t got, size_t want, size_t k = 0) {
	if (++failures < 20) {
		std::printf("MISMATCH %s lens %zu/%zu k=%zu got %zu want %zu\n", what, a.size(), b.size(), k, got, want);
		if (a.size() < 80 && b.size() < 80) {
			std::printf("  a=\"%s\"\n  b=\"%s\"\n", a.c_str(), b.c_str());
		}
	}
}

// Textbook two-row dynamic program, unit costs.
static size_t dp(std::string_view a, std::string_view b) {
	std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
	for (size_t j = 0; j <= b.size(); ++j) prev[j] = j;
	for (size_t i = 0; i < a.size(); ++i) {
		cur[0] = i + 1;
		for (size_t j = 0; j < b.size(); ++j) {
			cur[j + 1] = std::min({prev[j + 1] + 1, cur[j] + 1, prev[j] + (a[i] == b[j] ? 0 : 1)});
		}
		std::swap(prev, cur);
	}
	return prev[b.size()];
}

static std::string random_string(size_t len, unsigned alphabet, unsigned base = 'a') {
	std::string s(len, '\0');
	for (auto& c : s) c = static_cast<char>(base + rng() % alphabet);
	return s;
}

// `edits` random substitutions, insertions or deletions (kinds weighted by `kinds`: bit 0 sub, 1 ins, 2 del).
static std::string mutate(std::string s, size_t edits, unsigned alphabet, unsigned kinds = 7) {
	for (size_t k = 0; k < edits; ++k) {
		unsigned kind;
		do kind = rng() % 3; while (!(kinds >> kind & 1));
		const char c = static_cast<char>('a' + rng() % alphabet);
		if (kind == 0 && !s.empty()) s[rng() % s.size()] = c;
		else if (kind == 1) s.insert(s.begin() + rng() % (s.size() + 1), c);
		else if (kind == 2 && !s.empty()) s.erase(s.begin() + rng() % s.size());
	}
	return s;
}

static bool g_dp = true;  // check against the dynamic program (skipped for the very large pairs)

// Everything that must agree for one ordered pair.
static void check_pair(const std::string& a, const std::string& b) {
	const size_t frozen = frozen_main::levenshtein_unit(a, b);
	size_t want = frozen;
	if (g_dp) {
		want = dp(a, b);
		++checks;
		if (frozen != want) fail("frozen vs dp", a, b, frozen, want);
	}

	// The default entry point, and every first-band width (the retry path).
	++checks;
	if (size_t got = strsim_detail::levenshtein_unit(a, b); got != want) fail("levenshtein_unit", a, b, got, want);
	// (Fewer widths and bounds on the largest tables: every run there is a full pass.)
	const bool big = a.size() * b.size() > 4000000;
	static const size_t slacks[] = {0, 1, 7, 64, 200, 1000, 100000};
	for (size_t slack : slacks) {
		if (big && slack != 0 && slack != 64) continue;
		++checks;
		if (size_t got = strsim_detail::levenshtein_unit(a, b, slack); got != want) fail("levenshtein_unit slack", a, b, got, want, slack);
	}

	// The band itself: exact within the bound, an upper bound above it.
	const size_t delta = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
	std::vector<size_t> ks = {0, 1, delta, delta + 1, delta + 2, delta + 63, want, want + 1, 2 * want + 1, a.size() + b.size()};
	if (want > 1) {
		ks.push_back(want - 1);
		ks.push_back(want / 2);
	}
	if (big) ks = {delta, delta + 63, want, want - 1, want / 2, a.size() + b.size()};
	for (size_t k : ks) {
		bool exact = false;
		const size_t got = strsim_detail::levenshtein_unit_band(a, b, k, exact);
		++checks;
		if (want <= k) {
			if (got != want) fail("band within k", a, b, got, want, k);
		} else if (got <= k || got < want) {
			fail("band above k", a, b, got, want, k);
		}
		if (exact && got != want) fail("band exact flag", a, b, got, want, k);
	}

	// The public metric, both directions.
	Levenshtein lev(false);
	const size_t longer = std::max(a.size(), b.size());
	if (!a.empty() && !b.empty() && a != b) {
		const double d = (double)want / longer;
		for (auto* p : {&a, &b}) {
			const double got = lev.distance(*p, p == &a ? b : a);
			++checks;
			if (std::memcmp(&got, &d, sizeof d) != 0) fail("Levenshtein::distance", a, b, (size_t)(got * longer), want);
		}
	}
	++checks;
	if (size_t got = strsim_detail::levenshtein_unit(b, a); got != want) fail("levenshtein_unit swapped", b, a, got, want);
}

int main(int argc, char**) {
	const bool quick = argc > 1 || LEVENSHTEIN_TEST_SANITIZED;

	// The textbook program against the oracle in test/oracle.h, so it can be trusted below.
	for (int r = 0; r < 300; ++r) {
		const auto a = random_string(rng() % 40, 1 + rng() % 5), b = random_string(rng() % 40, 1 + rng() % 5);
		++checks;
		if (dp(a, b) != oracle::lev_raw(a, b, 1, 1)) fail("dp vs oracle", a, b, dp(a, b), oracle::lev_raw(a, b, 1, 1));
	}

	// Every combination of the lengths around the word edges and the long ones.
	std::vector<size_t> lens = {63, 64, 65, 127, 128, 129, 1000, 3000, 10000};
	if (quick) lens.pop_back();
	const unsigned alphabets[] = {2, 4, 26};
	for (size_t la : lens) {
		for (size_t lb : lens) {
			// The dynamic program is O(n*m): up to four million cells every pair runs it, up to
			// twelve million only the first two pairs, beyond that none (the frozen kernel, itself
			// checked against it below that size, is the reference).
			const size_t cells = la * lb;
			int dp_pairs = cells <= 4000000 ? 100 : cells <= 12000000 ? 2 : 0;
			auto check = [&](const std::string& x, const std::string& y) {
				g_dp = dp_pairs-- > 0;
				check_pair(x, y);
			};
			const unsigned alpha = alphabets[(la + lb) % 3];
			check(random_string(la, alpha), random_string(lb, alpha));
			// Near-identical: about 1% substitutions, then indels, then both, at the first length.
			const auto base = random_string(la, alpha);
			check(base, mutate(base, la / 100 + 1, alpha, 1));
			check(base, mutate(base, la / 100 + 1, alpha, 6));
			check(base, mutate(base, la / 20 + 1, alpha));
			check(base, random_string(lb, alpha));
			// A prefix of one inside the other, and a long run deleted from the middle.
			check(base, base.substr(0, std::min(la, lb)));
			check(base, base.substr(0, la / 3) + base.substr(la / 3 + std::min(la / 2, size_t(300))));
		}
	}
	// The dynamic program on the longest strings, once each.
	g_dp = true;
	if (!quick) {
		const auto base = random_string(10000, 26);
		check_pair(base, mutate(base, 100, 26));
		check_pair(random_string(10000, 4), random_string(10000, 4));
	}

	// Shapes that force the alignment far from the main diagonal.
	for (size_t len : {200u, 500u, 1300u}) {
		const auto x = random_string(len, 4), y = random_string(len / 2, 4);
		check_pair(x + y, y + x);                    // a rotation
		check_pair(x + y, y);                        // a long deletion at the front
		check_pair(y, y + x);                        // a long insertion at the end
		check_pair(x, std::string(len, 'a'));        // few distinct characters
		check_pair(std::string(len, 'a'), std::string(len + 70, 'a') + "b");
		check_pair(std::string(len, 'a'), std::string(len, 'b'));
		check_pair(std::string(len, 'a') + x, x + std::string(len, 'a'));
	}

	// An insertion and a deletion a few characters apart: distance 2, but the cheapest alignment
	// leaves the diagonal for a few rows, which the narrowest band cannot see.
	for (size_t len : {600u, 1000u}) {
		const auto x = random_string(len, 26);
		for (size_t gap = 1; gap <= 6; ++gap) {
			auto y = x;
			y.erase(len / 2, 1);
			y.insert(len / 2 + gap, 1, static_cast<char>('A' + rng() % 26));
			check_pair(x, y);
		}
	}

	// A mostly constant string with a few markers, one character deleted before the markers and
	// one inserted after them: distance 2, while staying on the diagonal costs two per marker.
	for (size_t len : {800u, 1500u}) {
		for (size_t markers : {2u, 3u, 5u, 7u}) {
			std::string x(len, 'a');
			for (size_t k = 0; k < markers; ++k) x[len / 10 + k * (len * 8 / 10) / markers] = 'b';
			auto y = x;
			y.erase(len / 20, 1);
			y.insert(len - len / 20, 1, 'a');
			check_pair(x, y);
			check_pair(y, x);
		}
	}

	// Random lengths, alphabets and edit counts.
	const int rounds = quick ? 300 : 1500;
	for (int r = 0; r < rounds; ++r) {
		const unsigned alpha = 1 + rng() % 30;
		const size_t la = 1 + rng() % 900, lb = 1 + rng() % 900;
		if (rng() % 2) {
			check_pair(random_string(la, alpha), random_string(lb, alpha));
		} else {
			const auto base = random_string(la, alpha);
			check_pair(base, mutate(base, rng() % (1 + la / (1 + rng() % 40)), alpha, 1 + rng() % 7));
		}
	}

	// Bytes beyond ASCII, a NUL, and case folding on long inputs through the public metric.
	for (int r = 0; r < (quick ? 3 : 10); ++r) {
		auto a = random_string(1500, 200, 1), b = mutate(a, 30, 200);
		for (auto& c : b) if (c > 'z') c = static_cast<char>(c ^ 0x80);
		a[rng() % a.size()] = '\0';
		check_pair(a, b);
		std::string mixed = random_string(2000, 52, 'A');
		for (auto& c : mixed) if (c > 'Z' && c < 'a') c = static_cast<char>(c + 32);
		std::string other = mutate(mixed, 25, 26);
		Levenshtein icase(true), plain(false);
		const std::string up = strings::upper(mixed), up_other = strings::upper(other);
		const double got = icase.distance(mixed, other), want = plain.distance(up, up_other);
		++checks;
		if (std::memcmp(&got, &want, sizeof got) != 0) fail("icase long", mixed, other, 0, 0);
	}

	std::printf("%zu checks, %zu failures\n", checks, failures);
	if (failures) return 1;
	std::printf("all long Levenshtein checks passed\n");
	return 0;
}
