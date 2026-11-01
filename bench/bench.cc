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


// Benchmark: this library vs the frozen legacy implementation vs
// rapidfuzz-cpp. rapidfuzz is a comparator only: it is fetched by the bench
// target (-DSTRING_SIMILARITY_BUILD_BENCH=ON) and is not a dependency of the
// library. Prints best-of-5 nanoseconds per call. Run it on an otherwise idle machine.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "levenshtein.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "sorensen_dice.h"
#include "../test/legacy_metrics.h"
#include "../test/frozen_dice.h"
#include "../test/frozen_levenshtein.h"
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

// A long Levenshtein row: the previous full-table block kernel (test/frozen_levenshtein.h),
// this library and rapidfuzz, each timed for about 30 ms per repeat.
static void long_row(const char* wl, const std::string& a, const std::string& b) {
	Levenshtein lev(false);
	const double want = lev.distance(a, b);
	if (std::fabs(rapidfuzz::levenshtein_normalized_distance(a, b) - want) > 1e-12 || frozen_main::levenshtein_unit(a, b) != std::size_t(std::lround(want * double(std::max(a.size(), b.size()))))) {
		std::printf("MISMATCH in %s\n", wl);
		std::exit(1);
	}
	const size_t n = 1 + size_t(0.03 / (std::max(1.0, bench(3, [&](size_t) { return lev.distance(a, b); })) * 1e-9));
	// The previous kernel behind the same equality check and normalization as the metric.
	const double main_kernel = bench(n, [&](size_t) { return a == b ? 0.0 : double(frozen_main::levenshtein_unit(a, b)) / double(std::max(a.size(), b.size())); });
	const double ours = bench(n, [&](size_t) { return lev.distance(a, b); });
	const double rf = bench(n, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(a, b); });
	std::printf("%-44s main %10.1f  new %9.1f  rapidfuzz %9.1f  | new/rapidfuzz %.2fx  main/new %.1fx\n", wl, main_kernel, ours, rf, ours / rf, main_kernel / ours);
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
	// rapidfuzz has no case folding: for the icase rows the comparator folds both strings first.
	auto rf_lev_folded = [&](auto& v) { return [&](size_t i) { return rapidfuzz::levenshtein_normalized_distance(strings::upper(std::string(v[i].first)), strings::upper(std::string(v[i].second))); }; };
	auto rf_jw_folded = [&](auto& v) { return [&](size_t i) { return rapidfuzz::jaro_winkler_similarity(strings::upper(std::string(v[i].first)), strings::upper(std::string(v[i].second)), 0.1); }; };

	for (int rep = 0; rep < 2; ++rep) {  // second row is the A/A rerun: the noise floor
		row(rep ? "Levenshtein random words (A/A rerun)" : "Levenshtein random words", bench(N, lev_pair(olev, rnd)), bench(N, lev_pair(lev, rnd)), bench(N, rf_lev(rnd)));
	}
	row("Levenshtein near words", bench(N, lev_pair(olev, near)), bench(N, lev_pair(lev, near)), bench(N, rf_lev(near)));
	row("Levenshtein random words icase=true (rapidfuzz folds first)", bench(N, lev_pair(olevi, rnd)), bench(N, lev_pair(levi, rnd)), bench(N, rf_lev_folded(rnd)));
	row("Levenshtein 300 chars, 4 letters", bench(500, [&](size_t) { return olev.distance(L1, L2); }), bench(5000, [&](size_t) { return lev.distance(L1, L2); }), bench(5000, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L1, L2); }));
	row("Levenshtein 3000 chars, 4 letters", bench(5, [&](size_t) { return olev.distance(L3, L4); }), bench(50, [&](size_t) { return lev.distance(L3, L4); }), bench(50, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L3, L4); }));
	row("Levenshtein 3000 chars, ~1% edits", bench(5, [&](size_t) { return olev.distance(L5, L6); }), bench(50, [&](size_t) { return lev.distance(L5, L6); }), bench(50, [&](size_t) { return rapidfuzz::levenshtein_normalized_distance(L5, L6); }));

	// Long strings against the previous full-table kernel ("main") and rapidfuzz.
	auto edit = [&](std::string s, size_t edits, unsigned alpha) {  // random substitutions, insertions and deletions
		for (size_t k = 0; k < edits; ++k) {
			const char c = char('a' + g() % alpha);
			switch (g() % 3) {
				case 0: s[g() % s.size()] = c; break;
				case 1: s.insert(s.begin() + g() % (s.size() + 1), c); break;
				default: s.erase(s.begin() + g() % s.size()); break;
			}
		}
		return s;
	};
	std::printf("-- long Levenshtein (ns per call), previous kernel vs this library vs rapidfuzz\n");
	long_row("Levenshtein 300 chars, 4 letters", L1, L2);
	long_row("Levenshtein 1000 chars, 4 letters", mk(1000, 4), mk(1000, 4));
	long_row("Levenshtein 3000 chars, 4 letters", L3, L4);
	long_row("Levenshtein 3000 chars, 26 letters", L5, mk(3000, 26));
	long_row("Levenshtein 3000 chars, ~1% substitutions", L5, L6);
	long_row("Levenshtein 3000 chars, ~1% edits (indels too)", L5, edit(L5, 30, 26));
	long_row("Levenshtein 3000 chars, ~5% edits, 4 letters", L3, edit(L3, 150, 4));
	long_row("Levenshtein 3000 chars, ~10% edits, 4 letters", L3, edit(L3, 300, 4));
	long_row("Levenshtein 3000 vs 3100 chars, 4 letters", L3, mk(3100, 4));
	{
		const std::string x = mk(10000, 26);
		long_row("Levenshtein 10000 chars, 26 letters", x, mk(10000, 26));
		long_row("Levenshtein 10000 chars, ~1% edits", x, edit(x, 100, 26));
	}
	std::printf("-- Jaro\n");

	row("Jaro random words", bench(N, sim_pair(ojaro, rnd)), bench(N, sim_pair(jaro, rnd)), bench(N, rf_jaro(rnd)));
	row("Jaro_Winkler random words", bench(N, sim_pair(ojw, rnd)), bench(N, sim_pair(jw, rnd)), bench(N, rf_jw(rnd)));
	row("Jaro_Winkler near words", bench(N, sim_pair(ojw, near)), bench(N, sim_pair(jw, near)), bench(N, rf_jw(near)));
	row("Jaro_Winkler near words icase=true (rapidfuzz folds first)", bench(N, sim_pair(ojwi, near)), bench(N, sim_pair(jwi, near)), bench(N, rf_jw_folded(near)));
	row("Jaro 300 chars", bench(500, [&](size_t) { return ojaro.similarity(L1, L2); }), bench(5000, [&](size_t) { return jaro.similarity(L1, L2); }), bench(5000, [&](size_t) { return rapidfuzz::jaro_similarity(L1, L2); }));
	row("Jaro 3000 chars", bench(5, [&](size_t) { return ojaro.similarity(L3, L4); }), bench(50, [&](size_t) { return jaro.similarity(L3, L4); }), bench(50, [&](size_t) { return rapidfuzz::jaro_similarity(L3, L4); }));

	// Related strings (a typo in a name or a sentence): the Jaro kernels strip the
	// common prefix, so these rows are where a strategy for near-identical input shows.
	struct Related { std::vector<std::string> a, b; };
	auto related = [&](size_t count, size_t lo, size_t hi, unsigned alpha, size_t edits) {
		Related r;
		for (size_t i = 0; i < count; ++i) {
			std::string s = mk(lo + g() % (hi - lo + 1), alpha), t = s;
			for (size_t e = 0; e < edits; ++e) t[g() % t.size()] = char('a' + g() % alpha);
			r.a.push_back(std::move(s));
			r.b.push_back(std::move(t));
		}
		return r;
	};
	const Related sentences = related(2000, 20, 40, 26, 2), r64 = related(2000, 64, 64, 26, 1), r65 = related(2000, 65, 65, 26, 1),
		r129 = related(1000, 129, 129, 26, 1), r300 = related(400, 300, 300, 26, 3);
	// 300 random bytes of 128 values above 0x7f: wide masks, no common prefix.
	Related high;
	for (int i = 0; i < 400; ++i) {
		std::string s(300, '\0'), t(300, '\0');
		for (auto& c : s) c = char(0x80 + g() % 128);
		for (auto& c : t) c = char(0x80 + g() % 128);
		high.a.push_back(std::move(s));
		high.b.push_back(std::move(t));
	}
	auto on = [&](const Related& r, auto&& f) { return [&r, f](size_t i) { return f(r.a[i % r.a.size()], r.b[i % r.a.size()]); }; };
	auto jaro_rows = [&](const char* name, const Related& r) {
		const size_t n = std::max<size_t>(r.a.size(), 20000 / (r.a[0].size() / 20 + 1));
		const std::string j = std::string("Jaro ") + name, w = std::string("Jaro_Winkler ") + name;
		row(j.c_str(), bench(n, on(r, [&](const std::string& a, const std::string& b) { return ojaro.similarity(a, b); })), bench(n, on(r, [&](const std::string& a, const std::string& b) { return jaro.similarity(a, b); })),
			bench(n, on(r, [&](const std::string& a, const std::string& b) { return rapidfuzz::jaro_similarity(a, b); })));
		row(w.c_str(), bench(n, on(r, [&](const std::string& a, const std::string& b) { return ojw.similarity(a, b); })), bench(n, on(r, [&](const std::string& a, const std::string& b) { return jw.similarity(a, b); })),
			bench(n, on(r, [&](const std::string& a, const std::string& b) { return rapidfuzz::jaro_winkler_similarity(a, b, 0.1); })));
	};
	jaro_rows("20-40 chars, 2 edits", sentences);
	jaro_rows("64 chars, 1 edit", r64);
	jaro_rows("65 chars, 1 edit", r65);
	jaro_rows("129 chars, 1 edit", r129);
	jaro_rows("300 chars, 3 edits", r300);
	jaro_rows("300 chars, high bytes", high);
	std::printf("(The legacy Jaro columns time the old, non-standard algorithm; the new Jaro/Jaro_Winkler match rapidfuzz.)\n");

	// Sorensen-Dice has no comparator here: new against the previous implementation.
	std::printf("-- Sorensen_Dice (bigram multiset), previous implementation vs this library\n");
	Sorensen_Dice dice(false), dicei(true);
	frozen_dice::Sorensen_Dice odice(false), odicei(true);
	auto dice_row = [&](const char* wl, size_t n, auto&& prev, auto&& ours) {
		const double p = bench(n, prev), o = bench(n, ours);
		std::printf("%-44s previous %10.1f  new %9.1f  | previous/new %.2fx\n", wl, p, o, p / o);
	};
	std::vector<std::pair<std::string, std::string>> tiny;
	for (size_t i = 0; i < 2000; ++i) tiny.push_back({mk(1 + g() % 2, 4), mk(1 + g() % 2, 4)});
	dice_row("Dice 1-2 chars", tiny.size(), [&](size_t i) { return odice.similarity(tiny[i].first, tiny[i].second); }, [&](size_t i) { return dice.similarity(tiny[i].first, tiny[i].second); });
	dice_row("Dice random words", N, sim_pair(odice, rnd), sim_pair(dice, rnd));
	dice_row("Dice near words", N, sim_pair(odice, near), sim_pair(dice, near));
	dice_row("Dice random words icase=true", N, sim_pair(odicei, rnd), sim_pair(dicei, rnd));
	dice_row("Dice 20-40 chars, 2 edits", 20000, on(sentences, [&](const std::string& a, const std::string& b) { return odice.similarity(a, b); }), on(sentences, [&](const std::string& a, const std::string& b) { return dice.similarity(a, b); }));
	dice_row("Dice 64 chars, 4 letters", 20000, [&](size_t i) { return odice.similarity(r64.a[i % 2000], r64.b[i % 2000]); }, [&](size_t i) { return dice.similarity(r64.a[i % 2000], r64.b[i % 2000]); });
	{
		const std::string a = mk(129, 4), b = mk(129, 4);
		dice_row("Dice 129 chars, 4 letters", 5000, [&](size_t) { return odice.similarity(a, b); }, [&](size_t) { return dice.similarity(a, b); });
	}
	dice_row("Dice 300 chars, 4 letters", 2000, [&](size_t) { return odice.similarity(L1, L2); }, [&](size_t) { return dice.similarity(L1, L2); });
	dice_row("Dice 3000 chars, 4 letters", 50, [&](size_t) { return odice.similarity(L3, L4); }, [&](size_t) { return dice.similarity(L3, L4); });
	return 0;
}
