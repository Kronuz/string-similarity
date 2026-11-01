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

#include <algorithm>                              // for std::min, std::max
#include <bit>                                    // for std::countr_zero
#include <cstddef>                                // for std::size_t
#include <string>                                 // for std::string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::forward

#include "basic_string_metric.h"
#include "bitparallel.h"


/*
 * The Jaro distance.
 *
 * Character-based metric.
 */
class Jaro : public StringMetric<Jaro> {
	friend class StringMetric<Jaro>;

	// The standard Jaro matching, in one pass: for the j-th character of str2,
	// take the first not yet used equal character of str1 inside
	// [j - bound, j + bound], where bound = max(|str1|, |str2|) / 2 - 1 (0 when
	// that would be negative). The candidates are found with bit masks: peq's row
	// for the character, minus the used bits, restricted to the window, lowest set
	// bit. The characters of str2 that matched are appended to `out` in str2
	// order; `used` marks the matched positions of str1. Returns how many matched.
	template <int Slot>
	static size_t match_characters(const strsim_detail::Peq<Slot>& peq, strsim_detail::word_t* used, size_t l_str1, std::string_view str2, size_t bound, char* out) {
		constexpr size_t W = strsim_detail::kWordBits;
		const size_t nb = peq.blocks();
		for (size_t w = 0; w < nb; ++w) {
			used[w] = 0;
		}

		size_t count = 0;
		size_t j = 0;

		if (nb == 1) {
			// One word: the used set lives in a register and the match is branch free.
			strsim_detail::word_t u = 0;
			for (const auto& c : str2) {
				const size_t start = j > bound ? j - bound : 0;
				const size_t end = std::min(j + bound + 1, l_str1);
				if (start < end) {
					const strsim_detail::word_t window = (~strsim_detail::word_t(0) << start) & (~strsim_detail::word_t(0) >> (W - end));
					const strsim_detail::word_t m = peq.row(static_cast<unsigned char>(c))[0] & ~u & window;
					u |= m & (~m + 1);  // lowest set bit, if any
					out[count] = c;
					count += (m != 0);
				}
				++j;
			}
			used[0] = u;
			return count;
		}

		size_t lo = 0;  // words below lo are completely used
		for (const auto& c : str2) {
			const size_t start = j > bound ? j - bound : 0;
			const size_t end = std::min(j + bound + 1, l_str1);
			if (start < end) {
				const auto* row = peq.row(static_cast<unsigned char>(c));
				while (lo < nb && ~used[lo] == 0) {
					++lo;
				}
				const size_t first = std::max(start / W, lo), lastw = (end - 1) / W;
				for (size_t w = first; w <= lastw; ++w) {
					strsim_detail::word_t m = row[w] & ~used[w];
					if (w == start / W) {
						m &= ~strsim_detail::word_t(0) << (start % W);
					}
					if (w == lastw && end % W != 0) {
						m &= (strsim_detail::word_t(1) << (end % W)) - 1;
					}
					if (m) {
						used[w] |= m & (~m + 1);  // lowest set bit
						out[count++] = c;
						break;
					}
				}
			}
			++j;
		}
		return count;
	}

public:
	// The Jaro similarity of two (already case-folded) strings.
	static double jaro(std::string_view str1, std::string_view str2) {
		const auto l_str1 = str1.size(), l_str2 = str2.size();
		if (!l_str1 || !l_str2) {
			return 0.0;
		}

		const size_t half = std::max(l_str1, l_str2) / 2;
		const size_t bound = half ? half - 1 : 0;

		constexpr size_t kStack = 512;
		char stack_buf[kStack];
		std::string heap_buf;
		char* common = stack_buf;
		if (l_str2 > kStack) {
			heap_buf.resize(l_str2);
			common = heap_buf.data();
		}

		const auto* p1 = reinterpret_cast<const unsigned char*>(str1.data());
		const size_t nb1 = (l_str1 + 63) / 64;
		strsim_detail::Peq<0> peq1(p1, l_str1, nb1);

		// common: str2's characters matched into str1, in str2 order; the used
		// positions of str1, in order, are the other half of the matching.
		auto* used = peq1.extra();
		const size_t m = match_characters(peq1, used, l_str1, str2, bound, common);
		if (!m) {
			return 0.0;
		}

		// Transpositions: half the matched characters that differ in order.
		size_t t = 0, k = 0;
		for (size_t w = 0; w < nb1; ++w) {
			for (auto bits = used[w]; bits; bits &= bits - 1) {
				t += str1[w * 64 + std::countr_zero(bits)] != common[k++];
			}
		}
		t /= 2;

		return (((double)m / l_str1) + ((double)m / l_str2) + ((double)(m - t) / m)) / 3.0;
	}

protected:
	double _similarity(std::string_view str1, std::string_view str2) const {
		return jaro(str1, str2);
	}

	// Case folding happens here, on the fly, rather than in the base class.
	double _icase_similarity(std::string_view str1, std::string_view str2, bool fold1, bool fold2) const {
		strsim_detail::Folded f1(str1, fold1), f2(str2, fold2);
		return _similarity(f1.view(), f2.view());
	}

	double _icase_distance(std::string_view str1, std::string_view str2, bool fold1, bool fold2) const {
		return 1.0 - _icase_similarity(str1, str2, fold1, fold2);
	}

	double _similarity(std::string_view str2) const {
		return _similarity(_str, str2);
	}

	double _distance(std::string_view str1, std::string_view str2) const {
		return 1.0 - _similarity(str1, str2);
	}

	double _distance(std::string_view str2) const {
		return 1.0 - _similarity(_str, str2);
	}

	std::string_view _name() const noexcept {
		return "Jaro";
	}

	std::string _description() const noexcept {
		return "Jaro";
	}

public:
	Jaro(bool icase=true)
		: StringMetric<Jaro>(icase) { }

	template <typename T>
		requires (!strsim_detail::is_metric_v<T, Jaro>)
	Jaro(T&& str, bool icase=true)
		: StringMetric<Jaro>(std::forward<T>(str), icase) { }
};
