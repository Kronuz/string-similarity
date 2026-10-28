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

static void row(const char* a, const char* b) {
	const std::string s1(a), s2(b);

	Levenshtein lev;
	Jaro jaro;
	Jaro_Winkler jw;
	LCSubstr lcsub;
	LCSubsequence lcseq;
	Jaccard jac;
	Sorensen_Dice sd;
	PhoneticMetric<SoundexEnglish, Levenshtein> sdx;

	std::printf("%-9s %-9s  lev=%.3f  jaro=%.3f  jw=%.3f  lcsubstr=%.3f  "
		"lcsubseq=%.3f  jaccard=%.3f  dice=%.3f  soundex=%.3f\n",
		a, b,
		lev.similarity(s1, s2),
		jaro.similarity(s1, s2),
		jw.similarity(s1, s2),
		lcsub.similarity(s1, s2),
		lcseq.similarity(s1, s2),
		jac.similarity(s1, s2),
		sd.similarity(s1, s2),
		sdx.similarity(s1, s2));
}

int main() {
	std::printf("String-similarity metrics (similarity in [0, 1], higher = closer):\n\n");
	row("kitten", "sitting");
	row("MARTHA", "MARHTA");
	row("Robert", "Rupert");
	row("night", "nacht");
	row("Dwayne", "Duane");
	return 0;
}
