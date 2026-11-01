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

#pragma once

#include <cstddef>                                // for std::size_t
#include <cstdint>                                // for std::uint32_t
#include <cstdlib>                                // for std::calloc, std::free
#include <new>                                    // for std::bad_alloc
#include <stdexcept>                              // for std::length_error
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::forward, std::swap

#include "basic_string_metric.h"
#include "bitparallel.h"


namespace strsim_detail {

// Per-thread table of bigram counts, indexed by the two bytes of a bigram. Every
// entry is zero between calls (a call clears what it touched). The 256 KiB is
// allocated on a thread's first use of Sorensen_Dice from zero pages, so only the
// pages a call touches become resident.
struct BigramCounts {
	std::uint32_t* counts;

	BigramCounts()
		: counts(static_cast<std::uint32_t*>(std::calloc(std::size_t(1) << 16, sizeof(std::uint32_t))))
	{
		if (!counts) {
			throw std::bad_alloc();
		}
	}
	BigramCounts(const BigramCounts&) = delete;
	BigramCounts& operator=(const BigramCounts&) = delete;
	~BigramCounts() { std::free(counts); }
};

inline std::uint32_t* bigram_counts() {
	static thread_local BigramCounts t;
	return t.counts;
}

inline unsigned bigram_key(const char* p) noexcept {
	return (unsigned(static_cast<unsigned char>(p[0])) << 8) | static_cast<unsigned char>(p[1]);
}

// The size of the multiset intersection of the bigrams of a and b, both at least
// two characters long: a repeated bigram counts as often as the operand with
// fewer occurrences has it. The shorter operand is counted into the table, the
// longer one consumes the counts, and the shorter one clears them again.
inline std::size_t bigram_intersection(std::string_view a, std::string_view b) {
	if (a.size() > b.size()) {
		std::swap(a, b);
	}
	const std::size_t na = a.size() - 1, nb = b.size() - 1;
	if (na > UINT32_MAX) {
		throw std::length_error("Sorensen_Dice: strings of 4 GiB or more are not supported");
	}
	std::uint32_t* const t = bigram_counts();
	for (std::size_t i = 0; i < na; ++i) {
		++t[bigram_key(a.data() + i)];
	}
	std::size_t shared = 0;
	for (std::size_t i = 0; i < nb; ++i) {
		std::uint32_t& c = t[bigram_key(b.data() + i)];
		const std::uint32_t hit = c != 0;
		c -= hit;
		shared += hit;
	}
	for (std::size_t i = 0; i < na; ++i) {
		t[bigram_key(a.data() + i)] = 0;
	}
	return shared;
}

}  // namespace strsim_detail


/*
 * Metric based in Sørensen–Dice index over the multiset of character bigrams.
 *
 * Token-based metric.
 */
class Sorensen_Dice : public StringMetric<Sorensen_Dice> {
	friend class StringMetric<Sorensen_Dice>;

	// 2 * |multiset intersection of the bigrams| / (|b1| + |b2|), for two (already
	// case-folded) strings. A string of one character has no bigram; two equal
	// ones are identical, which scores 1 like any other identical pair.
	static double dice(std::string_view str1, std::string_view str2) {
		if (str1.size() < 2 || str2.size() < 2) {
			return !str1.empty() && str1 == str2 ? 1.0 : 0.0;
		}
		return (2.0 * strsim_detail::bigram_intersection(str1, str2)) / ((str1.size() - 1) + (str2.size() - 1));
	}

	double _similarity(std::string_view str1, std::string_view str2) const {
		return dice(str1, str2);
	}

	double _similarity(std::string_view str2) const {
		return dice(_str, str2);
	}

	// Case folding happens here, on the fly, rather than in the base class.
	double _icase_similarity(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		strsim_detail::Folded f1(str1, fold_first), f2(str2, fold_second);
		return dice(f1.view(), f2.view());
	}

	double _icase_distance(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		return 1.0 - _icase_similarity(str1, str2, fold_first, fold_second);
	}

	/*
	 * It is not a proper distance metric as it does not possess
	 * the property of triangle inequality.
	 */

	double _distance(std::string_view str1, std::string_view str2) const {
		return 1.0 - _similarity(str1, str2);
	}

	double _distance(std::string_view str2) const {
		return 1.0 - _similarity(str2);
	}

	std::string_view _name() const noexcept {
		return "Sorensen_Dice";
	}

	std::string _description() const noexcept {
		return "Sorensen Dice";
	}

public:
	Sorensen_Dice(bool icase=true)
		: StringMetric<Sorensen_Dice>(icase) { }

	template <typename T>
		requires (!strsim_detail::is_metric_v<T, Sorensen_Dice>)
	Sorensen_Dice(T&& str, bool icase=true)
		: StringMetric<Sorensen_Dice>(std::forward<T>(str), icase) { }
};
