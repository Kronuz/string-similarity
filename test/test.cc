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

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "levenshtein.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "lcsubstr.h"
#include "lcsubsequence.h"
#include "jaccard.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"

#include "english_soundex.h"

static int checks = 0;

static void close_to(double got, double want, double tol, const char* what) {
	++checks;
	if (std::fabs(got - want) > tol) {
		std::printf("FAIL: %s: got %.6f, want %.6f (tol %.6f)\n", what, got, want, tol);
		assert(false && "value out of tolerance");
	}
}

int main() {
	const std::string kitten = "kitten";
	const std::string sitting = "sitting";

	// --- Levenshtein -------------------------------------------------------
	// The classic edit distance between "kitten" and "sitting" is 3
	// (k->s, e->i, +g). This class returns a NORMALIZED distance,
	// raw / (maxCost * max(len1, len2)) = 3 / (1 * 7). Recover the raw 3 by
	// multiplying back through max(len1, len2) = 7.
	{
		Levenshtein lev(/*icase*/ false);
		double norm = lev.distance(kitten, sitting);
		close_to(norm, 3.0 / 7.0, 1e-9, "Levenshtein normalized kitten/sitting");
		double raw = norm * 7.0;
		close_to(raw, 3.0, 1e-9, "Levenshtein raw distance kitten/sitting == 3");

		// similarity is 1 - normalized distance.
		close_to(lev.similarity(kitten, sitting), 1.0 - 3.0 / 7.0, 1e-9,
			"Levenshtein similarity kitten/sitting");

		// Identical strings: base case returns distance 0, similarity 1.
		close_to(lev.distance(kitten, kitten), 0.0, 1e-9, "Levenshtein equal distance 0");
		close_to(lev.similarity(kitten, kitten), 1.0, 1e-9, "Levenshtein equal similarity 1");
	}

	// --- Jaro / Jaro-Winkler ----------------------------------------------
	// The textbook pair MARTHA / MARHTA: Jaro 0.9444, and the Jaro-Winkler boost
	// (prefix "MAR", p = 0.1) lifts it to 0.9611. DWAYNE / DUANE: 0.8222 and 0.84.
	{
		Jaro jaro(/*icase*/ false);
		close_to(jaro.similarity(std::string("MARTHA"), std::string("MARHTA")),
			0.944444, 1e-4, "Jaro MARTHA/MARHTA ~0.9444");

		Jaro_Winkler jw(/*icase*/ false, /*p*/ 0.1, /*bt*/ 0.7);
		close_to(jw.similarity(std::string("MARTHA"), std::string("MARHTA")),
			0.961111, 1e-4, "Jaro_Winkler MARTHA/MARHTA ~0.9611");
		close_to(jaro.similarity(std::string("DWAYNE"), std::string("DUANE")),
			0.822222, 1e-4, "Jaro DWAYNE/DUANE ~0.8222");
		close_to(jw.similarity(std::string("DWAYNE"), std::string("DUANE")),
			0.84, 1e-4, "Jaro_Winkler DWAYNE/DUANE == 0.84");
	}

	// --- Longest Common Substring -----------------------------------------
	// "ABABC" vs "BABCA": longest common contiguous run is "BABC" (length 4).
	// similarity = lcs / max(len1, len2) = 4 / 5 = 0.8.
	{
		LCSubstr lcs(/*icase*/ false);
		close_to(lcs.similarity(std::string("ABABC"), std::string("BABCA")),
			4.0 / 5.0, 1e-9, "LCSubstr ABABC/BABCA == 4/5");
		close_to(lcs.distance(std::string("ABABC"), std::string("BABCA")),
			1.0 - 4.0 / 5.0, 1e-9, "LCSubstr distance ABABC/BABCA");
	}

	// --- Longest Common Subsequence ---------------------------------------
	// "ABCBDAB" vs "BDCAB": longest common (not necessarily contiguous)
	// subsequence has length 4 (e.g. "BCAB"). max len = 7, so 4/7.
	{
		LCSubsequence lcs(/*icase*/ false);
		close_to(lcs.similarity(std::string("ABCBDAB"), std::string("BDCAB")),
			4.0 / 7.0, 1e-9, "LCSubsequence ABCBDAB/BDCAB == 4/7");
	}

	// --- Jaccard (char sets) ----------------------------------------------
	// "night" -> {n,i,g,h,t}, "nacht" -> {n,a,c,h,t}.
	// intersection {n,h,t} = 3, union = 5 + 5 - 3 = 7, so 3/7.
	{
		Jaccard jac(/*icase*/ false);
		close_to(jac.similarity(std::string("night"), std::string("nacht")),
			3.0 / 7.0, 1e-9, "Jaccard night/nacht == 3/7");
		close_to(jac.distance(std::string("night"), std::string("nacht")),
			1.0 - 3.0 / 7.0, 1e-9, "Jaccard distance night/nacht");
	}

	// --- Sorensen-Dice (char bigrams) -------------------------------------
	// "night" bigrams {ni,ig,gh,ht}, "nacht" bigrams {na,ac,ch,ht}.
	// shared bigram {ht} = 1, so 2*1 / (4 + 4) = 2/8 = 0.25.
	{
		Sorensen_Dice sd(/*icase*/ false);
		close_to(sd.similarity(std::string("night"), std::string("nacht")),
			0.25, 1e-9, "Sorensen-Dice night/nacht == 0.25");
	}

	// --- Soundex-backed metric --------------------------------------------
	// PhoneticMetric<Encoder, Metric> encodes its inputs phonetically, then runs
	// the inner string metric over the codes, so homophones (which collapse to
	// the same Soundex code) score as more similar than words that merely look
	// alike but sound different. Construct it from one word: the constructor
	// stores _str = encode(word), and the inner Levenshtein then compares that
	// stored code against the encoded comparand. "Robert" and "Rupert" are
	// Soundex homophones; "Robert" and "Rubin" are not.
	{
		using SoundexLevenshtein = PhoneticMetric<SoundexEnglish, Levenshtein>;
		SoundexEnglish enc;

		// The encoder collapses the homophones to identical codes.
		assert(enc.encode(std::string("Robert")) == enc.encode(std::string("Rupert"))); ++checks;
		assert(enc.encode(std::string("Robert")) != enc.encode(std::string("Rubin"))); ++checks;

		// Through the metric: similarity of the phonetic codes. Homophones share a
		// code, so the inner metric sees two equal strings and reports 1.0; the
		// non-homophone pair scores strictly lower.
		Levenshtein lev_codes(/*icase*/ false);
		double homophones = lev_codes.similarity(
			enc.encode(std::string("Robert")), enc.encode(std::string("Rupert")));
		double nonhomophones = lev_codes.similarity(
			enc.encode(std::string("Robert")), enc.encode(std::string("Rubin")));

		close_to(homophones, 1.0, 1e-9, "Soundex homophones Robert/Rupert similarity 1");
		assert(homophones > nonhomophones); ++checks;

		// And the PhoneticMetric type instantiates and stores the encoded form.
		SoundexLevenshtein metric(std::string("Robert"));
		assert(metric.name() == enc.name()); ++checks;
	}

	std::printf("all string-metrics tests passed (%d checks)\n", checks);
	return 0;
}
