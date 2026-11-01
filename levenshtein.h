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
#include <cstddef>                                // for std::size_t
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::forward
#include <vector>                                 // for std::vector

#include "basic_string_metric.h"
#include "bitparallel.h"


/*
 * Levenshtein distance or edit distance.
 *
 * Character-based metric.
 */
class Levenshtein : public StringMetric<Levenshtein> {
protected:
	// protected (not private) so a consumer can subclass to re-add the
	// serialise()/unserialise() persistence this library deliberately omits.
	size_t _subst_cost;
	size_t _ins_del_cost;
	size_t _maxCost;

private:
	friend class StringMetric<Levenshtein>;

	// Weighted dynamic programming, O(len1 * len2). Used when the costs are not a
	// plain unit-cost edit distance (substitution != insert/delete, or zero
	// costs). Keeps the two columns on the stack for short strings.
	size_t _raw_weighted(std::string_view str1, std::string_view str2) const {
		const auto len1 = str1.size(), len2 = str2.size();
		constexpr size_t kStack = 256;
		size_t stack_buf[2 * (kStack + 1)];
		std::vector<size_t> heap_buf;
		size_t* col = stack_buf;
		if (len2 > kStack) {
			heap_buf.resize(2 * (len2 + 1));
			col = heap_buf.data();
		}
		size_t* prev_col = col + (len2 + 1);

		for (size_t i = 0; i <= len2; ++i) {
			prev_col[i] = i * _ins_del_cost;
		}

		for (size_t i = 0; i < len1; ++i) {
			col[0] = (i + 1) * _ins_del_cost;
			for (size_t j = 0; j < len2; ++j) {
				col[j + 1] = std::min({ prev_col[j + 1] + _ins_del_cost, col[j] + _ins_del_cost, prev_col[j] + (str1[i] == str2[j] ? 0 : _subst_cost) });
			}
			std::swap(col, prev_col);
		}

		return prev_col[len2];
	}

	// Both operands are already case-folded (if folding applies).
	double _distance(std::string_view str1, std::string_view str2) const {
		const auto len1 = str1.size(), len2 = str2.size();
		if (_maxCost == 0) {
			return 0.0;  // every edit is free: any two strings are at distance 0
		}
		size_t raw;
		if (_subst_cost == 1 && _ins_del_cost == 1) {
			// Plain unit costs (the default): bit-parallel edit distance. Other costs
			// use the weighted dynamic program.
			raw = strsim_detail::levenshtein_unit(str1, str2);
		} else {
			raw = _raw_weighted(str1, str2);
		}
		return (double)raw / (_maxCost * std::max(len1, len2));
	}

	// Case folding happens here, on the fly, rather than in the base class.
	double _icase_distance(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		strsim_detail::Folded f1(str1, fold_first), f2(str2, fold_second);
		return _distance(f1.view(), f2.view());
	}

	double _icase_similarity(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		return 1.0 - _icase_distance(str1, str2, fold_first, fold_second);
	}

	double _distance(std::string_view str2) const {
		return _distance(_str, str2);
	}

	double _similarity(std::string_view str1, std::string_view str2) const {
		return 1.0 - _distance(str1, str2);
	}

	double _similarity(std::string_view str2) const {
		return 1.0 - _distance(_str, str2);
	}

	std::string_view _name() const noexcept {
		return "Levenshtein";
	}

	std::string _description() const noexcept {
		return "Levenshtein";
	}

public:
	Levenshtein(bool icase=true, size_t subst_cost=1.0, size_t ins_del_cost=1.0)
		: StringMetric<Levenshtein>(icase),
		  _subst_cost(subst_cost),
		  _ins_del_cost(ins_del_cost),
		  _maxCost(std::max(_subst_cost, _ins_del_cost)) { }

	template <typename T>
		requires (!strsim_detail::is_metric_v<T, Levenshtein>)
	Levenshtein(T&& str, bool icase=true, size_t subst_cost=1.0, size_t ins_del_cost=1.0)
		: StringMetric<Levenshtein>(std::forward<T>(str), icase),
		  _subst_cost(subst_cost),
		  _ins_del_cost(ins_del_cost),
		  _maxCost(std::max(_subst_cost, _ins_del_cost)) { }
};
