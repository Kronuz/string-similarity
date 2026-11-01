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

#include <algorithm>                              // for std::min, std::mismatch
#include <cstddef>                                // for std::size_t
#include <stdexcept>                              // for std::invalid_argument
#include <string>                                 // for std::to_string
#include <string_view>                            // for std::string_view
#include <utility>                                // for std::forward

#include "jaro.h"


/*
 * The Jaro–Winkler distance.
 *
 * The Jaro similarity plus a boost for a shared prefix: when the Jaro
 * similarity jd is above the boost threshold `bt` (default 0.7), the result is
 * jd + l * p * (1 - jd), where l is the length of the common prefix (at most 4)
 * and p the scaling factor (default 0.1, at most 0.25).
 */
class Jaro_Winkler : public StringMetric<Jaro_Winkler> {
protected:
	// protected (not private) so a consumer can subclass to re-add the
	// serialise()/unserialise() persistence this library deliberately omits.
	double _p;  // Scaling factor.
	double _bt; // Boost threshold.

private:
	static constexpr size_t MAX_PREFIX_LEN{4};
	static constexpr double MAX_P{0.25};
	static constexpr double MAX_BT{1.0};

	friend class StringMetric<Jaro_Winkler>;

	static size_t len_common_prefix(std::string_view str1, std::string_view str2) {
		const size_t n = std::min({MAX_PREFIX_LEN, str1.size(), str2.size()});
		size_t l = 0;
		while (l < n && str1[l] == str2[l]) {
			++l;
		}
		return l;
	}

	// Both operands are already case-folded (if folding applies).
	double _winkler(std::string_view str1, std::string_view str2) const {
		const double jd = Jaro::jaro(str1, str2);

		if (jd <= _bt) {
			return jd;
		}

		return jd + (len_common_prefix(str1, str2) * _p * (1.0 - jd));
	}

	// Case folding happens here, on the fly, rather than in the base class.
	double _icase_similarity(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		strsim_detail::Folded f1(str1, fold_first), f2(str2, fold_second);
		return _winkler(f1.view(), f2.view());
	}

	double _icase_distance(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		return 1.0 - _icase_similarity(str1, str2, fold_first, fold_second);
	}

	double _similarity(std::string_view str1, std::string_view str2) const {
		return _winkler(str1, str2);
	}

	double _similarity(std::string_view str2) const {
		return _winkler(_str, str2);
	}

	double _distance(std::string_view str1, std::string_view str2) const {
		return 1.0 - _winkler(str1, str2);
	}

	double _distance(std::string_view str2) const {
		return 1.0 - _winkler(_str, str2);
	}

	std::string_view _name() const noexcept {
		return "Jaro_Winkler";
	}

	std::string _description() const noexcept {
		return "Jaro Winkler";
	}

	void _check() const {
		if (_p < 0.0 || _p > MAX_P) {
			throw std::invalid_argument("_p should be positive and not exceed " + std::to_string(MAX_P));
		}
		if (_bt < 0.0 || _bt > MAX_BT) {
			throw std::invalid_argument("_bt should be positive and not exceed " + std::to_string(MAX_BT));
		}
	}

public:
	Jaro_Winkler(bool icase=true, double p=0.1, double bt=0.7)
		: StringMetric<Jaro_Winkler>(icase),
		  _p(p),
		  _bt(bt)
	{
		_check();
	}

	template <typename T>
		requires (!strsim_detail::is_metric_v<T, Jaro_Winkler>)
	Jaro_Winkler(T&& str, bool icase=true, double p=0.1, double bt=0.7)
		: StringMetric<Jaro_Winkler>(std::forward<T>(str), icase),
		  _p(p),
		  _bt(bt)
	{
		_check();
	}
};
