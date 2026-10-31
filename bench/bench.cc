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


// Benchmark: this library (new kernels) vs the frozen legacy implementation vs
// rapidfuzz-cpp. rapidfuzz is a comparator only: it is fetched by the bench
// target (-DSTRING_SIMILARITY_BUILD_BENCH=ON) and is not a dependency of the
// library. Prints best-of-5 nanoseconds per call. Run it under `quiet strict`.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "levenshtein.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "../test/legacy_metrics.h"
#include "rapidfuzz/distance.hpp"

using clk = std::chrono::steady_clock;
static volatile double g_sink;

template <typename F>
static double bench(size_t n, F&& fn) {
	double best = 1e300;
	for (int r = 0; r < 5; ++r) {
		const auto t0 = clk::now();
		double s = 0;
		for (size_t i = 0; i < n; ++i) s += fn(i);
		const double t = std::chrono::duration<double>(clk::now() - t0).count();
		g_sink = s;
		if (t < best) best = t;
	}
	return best / double(n) * 1e9;
}

static void row(const char* wl, double legacy, double ours, double rf) {
	std::printf("%-44s legacy %10.1f  new %9.1f  rapidfuzz %9.1f  | new/rapidfuzz %.2fx  legacy/new %.1fx\n", wl, legacy, ours, rf, ours / rf, legacy / ours);
}

int main() {
	std::mt19937_64 g(12345);
	// Word-like strings: lower-case, 3..12 letters.
	const size_t N = 20000;
	std::vector<std::string> words(N), mut(N);
	for (auto& w : words) {
		w.resize(3 + g() % 10);
		for (auto& c : w) c = char('a' + g() % 26);
	}
	for (size_t i = 0; i < N; ++i) {
		mut[i] = words[i];
		mut[i][g() % mut[i].size()] = char('a' + g() % 26);
		if (g() & 1) mut[i].erase(g() % mut[i].size(), 1);
	}
	std::vector<std::pair<std::string_view, std::string_view>> rnd, near;
	for (size_t i = 0; i < N; ++i) {
		rnd.push_back({words[g() % N], words[g() % N]});
		near.push_back({words[i], mut[i]});
	}
	auto mk = [&](size_t n, unsigned alpha) { std::string s(n, 'a'); for (auto& c : s) c = char('a' + g() % alpha); return s; };
	const std::string L1 = mk(300, 4), L2 = mk(300, 4), L3 = mk(3000, 4), L4 = mk(3000, 4);
	std::string L5 = mk(3000, 26), L6 = L5;  // 3000 chars, ~1% edits
	for (int k = 0; k < 30; ++k) L6[g() % L6.size()] = char('a' + g() % 26);

	Levenshtein lev(false), levi(true);
	legacy::Levenshtein olev(false), olevi(true);
	Jaro jaro(false);
	legacy::Jaro ojaro(false);
	Jaro_Winkler jw(false), jwi(true);
	legacy::Jaro_Winkler ojw(false), ojwi(true);

	// Identity against the legacy implementation.
	size_t bad = 0;
	for (auto* v : {&rnd, &near}) for (auto& [a, b] : *v) {
		std::string x(a), y(b);
		if (lev.distance(x, y) != olev.distance(x, y)) ++bad;
		if (std::fabs(jaro.similarity(x, y) - rapidfuzz::jaro_similarity(x, y)) > 1e-12) ++bad;  // standard Jaro: the oracle is rapidfuzz
		if (std::fabs(jw.similarity(x, y) - rapidfuzz::jaro_winkler_similarity(x, y, 0.1)) > 1e-12) ++bad;
	}
	std::printf("identity (Levenshtein vs legacy, Jaro/Jaro_Winkler vs rapidfuzz): %zu mismatches\n", bad);
	if (bad) return 1;

	auto lev_pair = [&](auto& m, auto& v) { return [&](size_t i) { return m.distance(v[i].first, v[i].second); }; };
	auto sim_pair = [&](auto& m, auto& v) { return [&](size_t i) { return m.similarity(v[i].first, v[i].second); }; };
	auto rf_lev = [&](auto& v) { return [&](size_t i) { return rapidfuzz::levenshtein_normalized_distance(v[i].first, v[i].second); }; };
	auto rf_jaro = [&](auto& v) { return [&](size_t i) { return rapidfuzz::jaro_similarity(v[i].first, v[i].second); }; };
	auto rf_jw = [&](auto& v) { return [&](size_t i) { return rapidfuzz::jaro_winkler_similarity(v[i].first, v[i].second, 0.1); }; };

	for (int rep = 0; rep < 2; ++rep) {  // second row is the A/A rerun: the noise floor
		row(rep ? "Levenshtein random words (A/A rerun)" : "Levenshtein random words", bench(N, lev_pair(olev, rnd)), bench(N, lev_pair(lev, rnd)), bench(N, rf_lev(rnd)));
	}
	row("Levenshtein near words", bench(N, lev_pair(olev, near)), bench(N, lev_pair(lev, near)), bench(N, rf_lev(near)));
	row("Levenshtein random words icase=true", bench(N, lev_pair(olevi, rnd)), bench(N, lev_pair(levi, rnd)), bench(N, rf_lev(rnd)));
	row("Levenshtein 300 chars, 4 letters", bench(500, [&](size_t) { return olev.distance(L1, L2); }), bench(5000, [&](size_t) { return lev.distance(L1, L2); }), bench(5000, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L1, L2); }));
	row("Levenshtein 3000 chars, 4 letters", bench(5, [&](size_t) { return olev.distance(L3, L4); }), bench(50, [&](size_t) { return lev.distance(L3, L4); }), bench(50, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L3, L4); }));
	row("Levenshtein 3000 chars, ~1% edits", bench(5, [&](size_t) { return olev.distance(L5, L6); }), bench(50, [&](size_t) { return lev.distance(L5, L6); }), bench(50, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L5, L6); }));

	row("Jaro random words", bench(N, sim_pair(ojaro, rnd)), bench(N, sim_pair(jaro, rnd)), bench(N, rf_jaro(rnd)));
	row("Jaro_Winkler random words", bench(N, sim_pair(ojw, rnd)), bench(N, sim_pair(jw, rnd)), bench(N, rf_jw(rnd)));
	row("Jaro_Winkler near words", bench(N, sim_pair(ojw, near)), bench(N, sim_pair(jw, near)), bench(N, rf_jw(near)));
	row("Jaro_Winkler near words icase=true", bench(N, sim_pair(ojwi, near)), bench(N, sim_pair(jwi, near)), bench(N, rf_jw(near)));
	row("Jaro 300 chars", bench(500, [&](size_t) { return ojaro.similarity(L1, L2); }), bench(5000, [&](size_t) { return jaro.similarity(L1, L2); }), bench(5000, [&](size_t) { return rapidfuzz::jaro_similarity(L1, L2); }));
	row("Jaro 3000 chars", bench(5, [&](size_t) { return ojaro.similarity(L3, L4); }), bench(50, [&](size_t) { return jaro.similarity(L3, L4); }), bench(50, [&](size_t) { return rapidfuzz::jaro_similarity(L3, L4); }));
	std::printf("(The legacy Jaro columns time the old, non-standard algorithm; the new Jaro/Jaro_Winkler match rapidfuzz.)\n");
	return 0;
}
