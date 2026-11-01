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

// Copies and moves of bound metrics. A metric bound to a string is a value: a copy
// or a move, from a non-const lvalue, a const lvalue or an rvalue, scores like the
// original, and keeps doing so after the original is overwritten or destroyed, also
// when a vector of them regrows. The expected scores come from the oracles in
// test/oracle.h, computed from the strings alone.
// Named checks: copy_<metric>.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "jaccard.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "lcsubsequence.h"
#include "lcsubstr.h"
#include "levenshtein.h"
#include "oracle.h"
#include "phonetic_metric.h"
#include "sorensen_dice.h"
#include "english_soundex.h"

static int failures = 0;
static size_t checks = 0;

static void expect(double got, double want, const char* what, const char* how) {
	++checks;
	if (got != want) {
		if (++failures < 30) std::printf("FAIL %s (%s): got %.17g want %.17g\n", what, how, got, want);
	}
}

static void expect_near(double got, double want, const char* what, const char* how) {
	++checks;
	if (!(std::fabs(got - want) <= 1e-12)) {
		if (++failures < 30) std::printf("FAIL %s (%s): got %.17g want %.17g\n", what, how, got, want);
	}
}

// The strings: one on the heap past any small-string buffer and one inside it, with
// repeated bigrams and different from every probe below.
static const std::string kLong = "the quick brown fox jumps over the lazy dog, twice: the quick brown fox";
static const std::string kShort = "abcabc";
static const std::string kProbeLong = "the quick brown fax jumped over a lazy dog twice, the quick brown fox";
static const std::string kProbeShort = "abcabd";

// Each metric: the name, a way to bind it, and the expected similarity of two strings.
template <typename M>
static void check_copies(const char* what, double (*want)(const std::string&, const std::string&)) {
	static_assert(std::is_copy_constructible_v<M> && std::is_move_constructible_v<M>);
	static_assert(std::is_copy_assignable_v<M> && std::is_move_assignable_v<M>);
	for (const auto* s : {&kLong, &kShort}) {
		const std::string& probe = s == &kLong ? kProbeLong : kProbeShort;
		const std::string other(s->size(), 'z');     // the same size: assigning it reuses the buffer
		const double sim = want(*s, probe);
		const double dist = 1.0 - sim;

		M original(*s, false);
		expect(original.similarity(probe), sim, what, "original");

		M from_lvalue(original);                     // non-const lvalue: must be a copy, not a rebinding
		const M& cref = original;
		M from_const(cref);
		M from_rvalue(M(*s, false));                 // a move
		M assigned(other, false);
		assigned = original;                         // copy assignment
		M source(*s, false);
		M moved(std::move(source));                  // move from a named object
		M move_assigned(other, false);
		move_assigned = M(*s, false);

		// Overwrite the original in place: a copy that points into its source reads
		// the new contents now.
		M clobber(other, false);
		original = clobber;
		expect(from_lvalue.similarity(probe), sim, what, "copy from non-const lvalue");
		// The distance is computed on its own, so it equals 1 - similarity up to rounding.
		expect_near(from_lvalue.distance(probe), dist, what, "copy from non-const lvalue, distance");
		expect(from_const.similarity(probe), sim, what, "copy from const lvalue");
		expect(from_rvalue.similarity(probe), sim, what, "construct from rvalue");
		expect(moved.similarity(probe), sim, what, "move from named object");
		expect(assigned.similarity(probe), sim, what, "copy assignment");
		expect(move_assigned.similarity(probe), sim, what, "move assignment");

		// A vector of them regrows by moving: their small strings move into new storage.
		std::vector<M> v;
		for (int i = 0; i < 40; ++i) {
			v.emplace_back(*s, false);
			for (const auto& m : v) expect(m.similarity(probe), sim, what, "vector regrowth");
		}
		std::vector<M> w = v;                        // a copy of the vector
		v.clear();
		v.shrink_to_fit();
		for (const auto& m : w) expect(m.similarity(probe), sim, what, "vector copy");
	}
}

static double want_lev(const std::string& a, const std::string& b) { return 1.0 - oracle::lev_distance(a, b, 1, 1); }
static double want_jaro(const std::string& a, const std::string& b) { return oracle::jaro(a, b).value; }
static double want_jw(const std::string& a, const std::string& b) { return oracle::jaro_winkler(a, b); }
static double want_dice(const std::string& a, const std::string& b) { return oracle::dice_multiset(a, b); }
static double want_jaccard(const std::string& a, const std::string& b) { return oracle::jaccard(a, b); }
static double want_lcsq(const std::string& a, const std::string& b) { return double(oracle::lcs_subsequence(a, b)) / std::max(a.size(), b.size()); }
static double want_lcst(const std::string& a, const std::string& b) { return double(oracle::lcs_substring(a, b)) / std::max(a.size(), b.size()); }

// The phonetic wrapper over each inner metric: the expected score is the inner score of the codes.
template <typename Inner>
static void check_phonetic_copies(const char* what, double (*want)(const std::string&, const std::string&)) {
	SoundexEnglish enc;
	using P = PhoneticMetric<SoundexEnglish, Inner>;
	for (const char* word : {"Robert", "Rubin"}) {
		const std::string probe = "Rupert";
		const std::string a = enc.encode(std::string(word)), b = enc.encode(probe);
		const double sim = a == b ? 1.0 : want(a, b);
		P original{std::string(word)};
		P from_lvalue(original);
		P from_rvalue(P{std::string(word)});
		P other{std::string("Washington")};
		original = other;
		expect(from_lvalue.similarity(probe), sim, what, "phonetic copy from non-const lvalue");
		expect(from_rvalue.similarity(probe), sim, what, "phonetic move");
		std::vector<P> v;
		for (int i = 0; i < 20; ++i) v.emplace_back(std::string(word));
		for (const auto& m : v) expect(m.similarity(probe), sim, what, "phonetic vector regrowth");
	}
}

// A class derived from a metric copies too (the binding constructor must not take it).
struct DerivedDice : Sorensen_Dice {
	using Sorensen_Dice::Sorensen_Dice;
};

int main() {
	check_copies<Levenshtein>("copy_levenshtein", want_lev);
	check_copies<Jaro>("copy_jaro", want_jaro);
	check_copies<Jaro_Winkler>("copy_jaro_winkler", want_jw);
	check_copies<Sorensen_Dice>("copy_dice", want_dice);
	check_copies<Jaccard>("copy_jaccard", want_jaccard);
	check_copies<LCSubsequence>("copy_lcs", want_lcsq);
	check_copies<LCSubstr>("copy_lcs", want_lcst);
	check_phonetic_copies<Levenshtein>("copy_phonetic", want_lev);
	check_phonetic_copies<Sorensen_Dice>("copy_phonetic", want_dice);
	{
		DerivedDice d(kLong, false);
		DerivedDice copy(d);
		DerivedDice other(std::string(kLong.size(), 'z'), false);
		d = other;
		expect(copy.similarity(kProbeLong), want_dice(kLong, kProbeLong), "copy_dice", "derived class");
	}
	std::printf("%zu checks, %d failures\n", checks, failures);
	if (failures) return 1;
	std::printf("all copy checks passed\n");
	return 0;
}
