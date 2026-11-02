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
#include <string_view>

#include "levenshtein.h"
#include "jaro.h"
#include "jaro_winkler.h"
#include "lcsubstr.h"
#include "lcsubsequence.h"
#include "jaccard.h"
#include "sorensen_dice.h"
#include "phonetic_metric.h"

#include "english_soundex.h"
#include "french_soundex.h"
#include "german_soundex.h"
#include "spanish_soundex.h"


static int checks = 0;

static void close_to(double got, double want, double tol, const char* what) {
	++checks;
	if (std::fabs(got - want) > tol) {
		std::printf("FAIL: %s: got %.6f, want %.6f (tol %.6f)\n", what, got, want, tol);
		assert(false && "value out of tolerance");
	}
}

// A bound metric scores a std::string_view comparand exactly as it scores the
// same text as a std::string, for lvalue, const and rvalue views and for a view
// cut out of a longer, non-NUL-terminated buffer.
template <typename M>
static void bound_views(M& metric, const std::string& word, const char* what) {
	const std::string padded = word + "xyz";
	const std::string_view cut = std::string_view(padded).substr(0, word.size());
	std::string_view lvalue = cut;
	const std::string_view clvalue = cut;
	const double d = metric.distance(std::string(word));
	const double s = metric.similarity(std::string(word));
	close_to(metric.distance(lvalue), d, 1e-12, what);
	close_to(metric.distance(clvalue), d, 1e-12, what);
	close_to(metric.distance(std::string_view(cut)), d, 1e-12, what);
	close_to(metric.similarity(lvalue), s, 1e-12, what);
	close_to(metric.similarity(clvalue), s, 1e-12, what);
	close_to(metric.similarity(std::string_view(cut)), s, 1e-12, what);
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

	// --- Bound metrics take std::string_view -----------------------------
	{
		for (bool icase : {false, true}) {
			Levenshtein lev("kitten", icase);
			Jaro jaro("kitten", icase);
			Jaro_Winkler jw("kitten", icase);
			LCSubstr lcstr("kitten", icase);
			LCSubsequence lcseq("kitten", icase);
			Jaccard jac("kitten", icase);
			Sorensen_Dice dice("kitten", icase);
			bound_views(lev, "Sitting", "Levenshtein bound view");
			bound_views(jaro, "Sitting", "Jaro bound view");
			bound_views(jw, "Sitting", "Jaro_Winkler bound view");
			bound_views(lcstr, "Sitting", "LCSubstr bound view");
			bound_views(lcseq, "Sitting", "LCSubsequence bound view");
			bound_views(jac, "Sitting", "Jaccard bound view");
			bound_views(dice, "Sitting", "Sorensen_Dice bound view");
		}

		// Construction from a C string, a view or a std::string folds case alike.
		close_to(Levenshtein("kitten", true).distance(std::string("KITTEN")), 0.0, 1e-12, "icase ctor C string");
		close_to(Levenshtein(std::string_view("kitten"), true).distance(std::string("KITTEN")), 0.0, 1e-12, "icase ctor view");
		close_to(Levenshtein(std::string("kitten"), true).distance(std::string_view("KITTEN")), 0.0, 1e-12, "icase ctor string");
		close_to(Levenshtein("kitten", false).distance(std::string_view("KITTEN")) > 0.0 ? 1.0 : 0.0, 1.0, 1e-12, "case-sensitive ctor");

		// Every language's encoder, with two inner metrics, built from a word and a view.
		PhoneticMetric<SoundexEnglish, Levenshtein> en("Robert");
		PhoneticMetric<SoundexFrench, Levenshtein> fr("Bonjour");
		PhoneticMetric<SoundexGerman, Levenshtein> de("Wikipedia");
		PhoneticMetric<SoundexSpanish, Levenshtein> es("Gonzalez");
		PhoneticMetric<SoundexFrench, LCSubsequence> fr_lcs("Bonjour");
		PhoneticMetric<SoundexGerman, LCSubsequence> de_lcs("Wikipedia");
		PhoneticMetric<SoundexSpanish, LCSubsequence> es_lcs("Gonzalez");
		bound_views(en, "Rupert", "PhoneticMetric English bound view");
		bound_views(fr, "Bonjur", "PhoneticMetric French bound view");
		bound_views(de, "Wikipedya", "PhoneticMetric German bound view");
		bound_views(es, "Gonsales", "PhoneticMetric Spanish bound view");
		bound_views(fr_lcs, "Bonjur", "PhoneticMetric French LCSubsequence bound view");
		bound_views(de_lcs, "Wikipedya", "PhoneticMetric German LCSubsequence bound view");
		bound_views(es_lcs, "Gonsales", "PhoneticMetric Spanish LCSubsequence bound view");

		// The French pair sounds alike, so the view scores 1.0 against its twin.
		close_to(fr.similarity(std::string_view("Bonjur")), 1.0, 1e-12, "French homophone view");
		// A bound metric is also constructible from a view.
		PhoneticMetric<SoundexSpanish, Levenshtein> from_view(std::string_view("Vaca"));
		close_to(from_view.similarity(std::string_view("Baca")), 1.0, 1e-12, "Spanish V/B view");
	}

	std::printf("all string-metrics tests passed (%d checks)\n", checks);
	return 0;
}
