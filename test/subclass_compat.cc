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

// The protected state of the metrics is part of the contract. The library has no
// persistence; a consumer derives from a metric, reads the members it needs to write
// the metric out (the bound string, the case flag, the costs, the Winkler factors, the
// encoder) and builds an equal metric from what it read.
//
// Each class below reads the members a consumer reads. The test stops compiling when a
// member becomes private or is renamed, and fails when a member holds something else
// than the constructor arguments. A derived metric must also give what its base gives.
// Named checks: bound_string, case_flag, costs, winkler_factors, encoder, same_results, rebuild.

#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

#include "english_soundex.h"
#include "jaccard.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "lcsubsequence.h"
#include "lcsubstr.h"
#include "levenshtein.h"
#include "phonetic_metric.h"
#include "sorensen_dice.h"

static size_t checks = 0;
static int failures = 0;

static void expect(bool ok, const char* what, const std::string& detail) {
	++checks;
	if (!ok) {
		if (++failures < 30) std::printf("FAIL %s: %s\n", what, detail.c_str());
	}
}

static void expect_near(double got, double want, const char* what, const std::string& detail) {
	expect(std::fabs(got - want) <= 1e-3, what, detail + ": got " + std::to_string(got) + " want " + std::to_string(want));
}

// The members every metric inherits from StringMetric.
template <typename M>
struct Reader : M {
	using M::M;
	bool icase() const { return this->_icase; }
	const std::string& bound() const { return this->_str; }
};

struct LevenshteinReader : Reader<Levenshtein> {
	using Reader<Levenshtein>::Reader;
	size_t subst_cost() const { return _subst_cost; }
	size_t ins_del_cost() const { return _ins_del_cost; }
	size_t max_cost() const { return _maxCost; }
};

struct WinklerReader : Reader<Jaro_Winkler> {
	using Reader<Jaro_Winkler>::Reader;
	double p() const { return _p; }
	double bt() const { return _bt; }
};

using PhoneticLevenshtein = PhoneticMetric<SoundexEnglish, Levenshtein>;

struct PhoneticReader : Reader<PhoneticLevenshtein> {
	using Reader<PhoneticLevenshtein>::Reader;
	const SoundexEnglish& encoder() const { return _soundex; }
};

static const char* const kProbes[] = {"nacht", "knight", "night", "NIGHT", "martha", "marhta", "dwayne", "duane", "x"};

// The bound string is folded to upper case when the metric ignores case and kept
// otherwise; the case flag is the constructor's; a metric without a string has none.
template <typename M>
static void check_state(const char* what) {
	Reader<M> folded(std::string("Night"));
	expect(folded.icase(), "case_flag", std::string(what) + " default ignores case");
	expect(folded.bound() == "NIGHT", "bound_string", std::string(what) + " folded: " + folded.bound());

	Reader<M> kept(std::string("Night"), false);
	expect(!kept.icase(), "case_flag", std::string(what) + " case sensitive");
	expect(kept.bound() == "Night", "bound_string", std::string(what) + " kept: " + kept.bound());

	Reader<M> empty(false);
	expect(!empty.icase(), "case_flag", std::string(what) + " unbound");
	expect(empty.bound().empty(), "bound_string", std::string(what) + " unbound string");
}

// A metric derived from M scores what M scores, bound or not, and carries M's name.
template <typename M>
static void check_same_results(const char* what) {
	M base(std::string("night"));
	Reader<M> derived(std::string("night"));
	M free_base;
	Reader<M> free_derived;
	expect(std::string(derived.name()) == std::string(base.name()), "same_results", std::string(what) + " name");
	for (const char* probe : kProbes) {
		const std::string p(probe);
		expect(derived.similarity(p) == base.similarity(p), "same_results", std::string(what) + " bound similarity of " + p);
		expect(derived.distance(p) == base.distance(p), "same_results", std::string(what) + " bound distance of " + p);
		const std::string q("night");
		expect(free_derived.similarity(q, p) == free_base.similarity(q, p), "same_results", std::string(what) + " similarity of " + p);
		expect(free_derived.distance(q, p) == free_base.distance(q, p), "same_results", std::string(what) + " distance of " + p);
	}
}

template <typename M>
static void check_metric(const char* what) {
	check_state<M>(what);
	check_same_results<M>(what);
}

static void check_levenshtein() {
	LevenshteinReader defaults(std::string("kitten"));
	expect(defaults.subst_cost() == 1 && defaults.ins_del_cost() == 1 && defaults.max_cost() == 1, "costs",
		"defaults " + std::to_string(defaults.subst_cost()) + "/" + std::to_string(defaults.ins_del_cost()) + "/" + std::to_string(defaults.max_cost()));

	LevenshteinReader costs(std::string("kitten"), false, 2, 3);
	expect(costs.subst_cost() == 2 && costs.ins_del_cost() == 3 && costs.max_cost() == 3, "costs",
		"given " + std::to_string(costs.subst_cost()) + "/" + std::to_string(costs.ins_del_cost()) + "/" + std::to_string(costs.max_cost()));

	LevenshteinReader unbound(false, 4, 5);
	expect(unbound.subst_cost() == 4 && unbound.ins_del_cost() == 5 && unbound.max_cost() == 5, "costs",
		"unbound " + std::to_string(unbound.subst_cost()) + "/" + std::to_string(unbound.ins_del_cost()) + "/" + std::to_string(unbound.max_cost()));

	// A consumer writes the five values out and builds the metric again from them.
	Levenshtein rebuilt(costs.bound(), costs.icase(), costs.subst_cost(), costs.ins_del_cost());
	for (const char* probe : kProbes) {
		const std::string p(probe);
		expect(rebuilt.similarity(p) == costs.similarity(p), "rebuild", "Levenshtein " + p);
		expect(rebuilt.distance(p) == costs.distance(p), "rebuild", "Levenshtein distance " + p);
	}
}

static void check_winkler() {
	WinklerReader defaults(std::string("martha"));
	expect(defaults.p() == 0.1 && defaults.bt() == 0.7, "winkler_factors",
		"defaults " + std::to_string(defaults.p()) + "/" + std::to_string(defaults.bt()));
	expect_near(defaults.similarity(std::string("marhta")), 0.9611, "same_results", "Jaro_Winkler martha/marhta");
	// The boost is Jaro_Winkler's own: a Jaro over the same pair scores lower.
	expect_near(Reader<Jaro>(std::string("martha")).similarity(std::string("marhta")), 0.9444, "same_results", "Jaro martha/marhta");

	WinklerReader unbound(true);
	expect(unbound.p() == 0.1 && unbound.bt() == 0.7, "winkler_factors", "unbound defaults");

	WinklerReader given(std::string("martha"), false, 0.2, 0.9);
	expect(given.p() == 0.2 && given.bt() == 0.9, "winkler_factors",
		"given " + std::to_string(given.p()) + "/" + std::to_string(given.bt()));
	expect(!given.icase() && given.bound() == "martha", "winkler_factors", "given case and string");

	Jaro_Winkler rebuilt(given.bound(), given.icase(), given.p(), given.bt());
	for (const char* probe : kProbes) {
		const std::string p(probe);
		expect(rebuilt.similarity(p) == given.similarity(p), "rebuild", "Jaro_Winkler " + p);
	}
}

static void check_phonetic() {
	PhoneticReader reader(std::string("Robert"));
	expect(reader.encoder().encode() == "R901096", "encoder", "code " + reader.encoder().encode());
	expect(reader.bound() == reader.encoder().encode(), "bound_string", "the bound string is the code: " + reader.bound());
	expect(!reader.icase(), "case_flag", "phonetic metrics compare codes as they are");

	PhoneticLevenshtein base(std::string("Robert"));
	for (const char* probe : {"Rupert", "Rubin", "Ashcraft", "Robert"}) {
		const std::string p(probe);
		expect(reader.similarity(p) == base.similarity(p), "same_results", "PhoneticMetric " + p);
	}
	expect(std::string(reader.name()) == std::string(base.name()), "same_results", "PhoneticMetric name");

	// A consumer builds the metric again from the word the encoder holds.
	PhoneticLevenshtein rebuilt(std::string("Robert"));
	expect(rebuilt.similarity(std::string("Rupert")) == 1.0, "rebuild", "Robert and Rupert share a code");
}

int main() {
	check_metric<Levenshtein>("Levenshtein");
	check_metric<Jaro>("Jaro");
	check_metric<Jaro_Winkler>("Jaro_Winkler");
	check_metric<Sorensen_Dice>("Sorensen_Dice");
	check_metric<Jaccard>("Jaccard");
	check_metric<LCSubstr>("LCSubstr");
	check_metric<LCSubsequence>("LCSubsequence");
	check_levenshtein();
	check_winkler();
	check_phonetic();
	std::printf("%zu checks, %d failures\n", checks, failures);
	if (failures) return 1;
	std::printf("all subclass checks passed\n");
	return 0;
}
