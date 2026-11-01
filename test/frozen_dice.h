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

// Frozen copy of Sorensen_Dice as it was when the multiset of bigrams was
// introduced (the sorted vector of bigram views and std::set_intersection). The
// differential test (test/differential.cc) compares the current Sorensen_Dice with it.
// Never edit it.

#pragma once

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "basic_string_metric.h"

namespace frozen_dice {

class Sorensen_Dice : public StringMetric<Sorensen_Dice> {
	std::vector<std::string_view> _str_bigrams;

	friend class StringMetric<Sorensen_Dice>;

	static std::vector<std::string_view> get_bigrams(std::string_view str) {
		std::vector<std::string_view> bigrams;
		for (size_t i = 0; i + 1 < str.size(); ++i) {
			bigrams.push_back(str.substr(i, 2));
		}
		std::sort(bigrams.begin(), bigrams.end());
		return bigrams;
	}

	static double dice(const std::vector<std::string_view>& b1, const std::vector<std::string_view>& b2) {
		if (b1.empty() || b2.empty()) {
			return 0.0;
		}
		Counter c;
		std::set_intersection(b1.begin(), b1.end(), b2.begin(), b2.end(), std::back_inserter(c));
		return (2.0 * c.count) / (b1.size() + b2.size());
	}

	double _similarity(std::string_view str1, std::string_view str2) const {
		return dice(get_bigrams(str1), get_bigrams(str2));
	}

	double _similarity(std::string_view str2) const {
		return dice(_str_bigrams, get_bigrams(str2));
	}

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
	Sorensen_Dice(T&& str, bool icase=true)
		: StringMetric<Sorensen_Dice>(std::forward<T>(str), icase),
		  _str_bigrams(get_bigrams(_str)) { }
};

}  // namespace frozen_dice
