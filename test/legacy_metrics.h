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

/*
 * Frozen copy of the pre-optimization implementations (basic_string_metric.h,
 * levenshtein.h, jaro.h, jaro_winkler.h as of commit ddfb8d1), renamed into
 * namespace legacy. The differential tests compare the current metrics against
 * these bit for bit. Do not edit: its whole purpose is to stay the same.
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "strings.hh"

namespace legacy {

// ---- basic_string_metric.h ----



#include "strings.hh"                             // for strings::upper


/*
 * Interface for make string metrics.
 */
template <typename Impl>
class StringMetric {
protected:
	bool _icase;
	std::string _str;

public:
	StringMetric(bool icase)
		: _icase(icase) { }

	template <typename T>
	StringMetric(T&& str, bool icase)
		: _icase(icase),
		  _str(_icase ? strings::upper(std::forward<T>(str)) : std::forward<T>(str)) { }

	template <typename T>
	double distance(T&& str1, T&& str2) const {
		// Check base cases.
		if (str1.empty() || str2.empty()) {
			return 1.0;
		}

		if (str1 == str2) {
			return 0.0;
		}

		return static_cast<const Impl*>(this)->_distance(
			_icase ? strings::upper(std::forward<T>(str1)) : std::forward<T>(str1),
			_icase ? strings::upper(std::forward<T>(str2)) : std::forward<T>(str2)
		);
	}

	template <typename T>
	double distance(T&& str2) const {
		// Check base cases.
		if (_str.empty() || str2.empty()) {
			return 1.0;
		}

		if (_str == str2) {
			return 0.0;
		}

		return static_cast<const Impl*>(this)->_distance(
			_icase ? strings::upper(std::forward<T>(str2)) : std::forward<T>(str2)
		);
	}

	template <typename T>
	double similarity(T&& str1, T&& str2) const {
		// Check base cases.
		if (str1.empty() || str2.empty()) {
			return 0.0;
		}

		if (str1 == str2) {
			return 1.0;
		}

		return static_cast<const Impl*>(this)->_similarity(
			_icase ? strings::upper(std::forward<T>(str1)) : std::forward<T>(str1),
			_icase ? strings::upper(std::forward<T>(str2)) : std::forward<T>(str2)
		);
	}

	template <typename T>
	double similarity(T&& str2) const {
		// Check base cases.
		if (_str.empty() || str2.empty()) {
			return 0.0;
		}

		if (_str == str2) {
			return 1.0;
		}

		return static_cast<const Impl*>(this)->_similarity(
			_icase ? strings::upper(std::forward<T>(str2)) : std::forward<T>(str2)
		);
	}

	std::string_view name() const noexcept {
		return static_cast<const Impl*>(this)->_name();
	}

	std::string description() const noexcept {
		auto desc = static_cast<const Impl*>(this)->_description();
		desc.append(_icase ? " ignore case" : " case sensitive");
		return desc;
	}
};


/*
 * Struct used for counting the times that push_back is called.
 *
 * Designed for using with std::set_intersection.
 */
struct Counter {
	size_t count = 0;

	struct value_type {
		template<typename T>
		value_type(const T&) { }
	};

	void push_back(const value_type&) {
		++count;
	}
};

// ---- levenshtein.h ----





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

	double _distance(std::string_view str1, std::string_view str2) const {
		const auto len1 = str1.size(), len2 = str2.size();
		std::vector<size_t> col(len2 + 1), prev_col(len2 + 1);

		for (size_t i = 0; i <= len2; ++i) {
			prev_col[i] = i * _ins_del_cost;
		}

		for (size_t i = 0; i < len1; ++i) {
			col[0] = i + 1;
			for (size_t j = 0; j < len2; ++j) {
				col[j + 1] = std::min({ prev_col[j + 1] + _ins_del_cost, col[j] + _ins_del_cost, prev_col[j] + (str1[i] == str2[j] ? 0 : _subst_cost) });
			}
			col.swap(prev_col);
		}

		return (double)prev_col[len2] / (_maxCost * std::max(len1, len2));
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
	Levenshtein(T&& str, bool icase=true, size_t subst_cost=1.0, size_t ins_del_cost=1.0)
		: StringMetric<Levenshtein>(std::forward<T>(str), icase),
		  _subst_cost(subst_cost),
		  _ins_del_cost(ins_del_cost),
		  _maxCost(std::max(_subst_cost, _ins_del_cost)) { }
};

// ---- jaro.h ----





/*
 * The Jaro distance.
 *
 * Character-based metric.
 */
class Jaro : public StringMetric<Jaro> {
	friend class StringMetric<Jaro>;

	std::vector<char> get_common_characters(std::string_view str1, std::string_view str2, size_t max_separation) const {
		const auto l_str1 = str1.size();
		std::vector<char> common_chars;
		common_chars.reserve(l_str1);
		std::vector<bool> proc_str1(l_str1, false);

		// Calculate matching characters.
		size_t i = 0;
		for (const auto& c : str2) {
			size_t start = std::max((int)(i - max_separation), 0);
			for (auto end = std::min(i + max_separation + 1, l_str1); start < end; ++start) {
				if (c == str1[start] && !proc_str1[start]) {
					proc_str1[start] = true;
					common_chars.push_back(c);
					break;
				}
			}
			++i;
		}

		return common_chars;
	}

protected:
	double _similarity(std::string_view str1, std::string_view str2) const {
		const auto l_str1 = str1.size(), l_str2 = str2.size();

		const auto max_separation = std::max((size_t)0, std::max(l_str1, l_str2) / 2 - 1);

		const auto common_str1 = get_common_characters(str1, str2, max_separation);
		const auto common_str2 = get_common_characters(str2, str1, max_separation);

		auto m1 = common_str1.size(), m2 = common_str2.size();
		if (!m1 || !m2) {
			return 0.0;
		}

		// Calculate character transpositions.
		size_t t = 0;
		if (m1 < m2) {
			auto it = common_str2.begin();
			for (const auto& c : common_str1) {
				t += *it != c;
				++it;
			}
		} else {
			m1 = m2;
			auto it = common_str1.begin();
			for (const auto& c : common_str2) {
				t += *it != c;
				++it;
			}
		}

		return (((double)m1 / l_str1) + ((double)m1 / l_str2) + ((m1 - t / 2.0) / m1)) / 3.0;
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
	Jaro(T&& str, bool icase=true)
		: StringMetric<Jaro>(std::forward<T>(str), icase) { }
};

// ---- jaro_winkler.h ----





/*
 * The Jaro–Winkler distance.
 *
 * It is a variant of the Jaro distance metric.
 */
class Jaro_Winkler : public Jaro {
protected:
	// protected (not private) so a consumer can subclass to re-add the
	// serialise()/unserialise() persistence this library deliberately omits.
	double _p;  // Scaling factor.
	double _bt; // Boost threshold.

private:
	const size_t MAX_PREFIX_LEN{4};
	const double MAX_P{0.25};
	const double MAX_BT{1.0};

	friend Jaro;

	size_t len_common_prefix(std::string_view str1, std::string_view str2) const {
		if (str1.size() > str2.size()) {
			return std::min(MAX_PREFIX_LEN, (size_t)std::distance(str2.begin(), std::mismatch(str2.begin(), str2.end(), str1.begin()).first));
		} else {
			return std::min(MAX_PREFIX_LEN, (size_t)std::distance(str1.begin(), std::mismatch(str1.begin(), str1.end(), str2.begin()).first));
		}
	}

	double _similarity(std::string_view str1, std::string_view str2) const {
		const double jd = Jaro::_similarity(str1, str2);

		if (jd < _bt) {
			return jd;
		}

		return jd + (len_common_prefix(str1, str2) * _p * (1.0 - jd));
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
		return "Jaro_Winkler";
	}

	std::string _description() const noexcept {
		return "Jaro Winkler";
	}

public:
	Jaro_Winkler(bool icase=true, double p=0.1, double bt=0.7)
		: Jaro(icase),
		  _p(p),
		  _bt(bt) { }

	template <typename T>
	Jaro_Winkler(T&& str, bool icase=true, double p=0.1, double bt=0.7)
		: Jaro(std::forward<T>(str), icase),
		  _p(p),
		  _bt(bt)
	{
		if (_p < 0.0 || _p > MAX_P) {
			throw std::invalid_argument("_p should be positive and not exceed " + std::to_string(MAX_P));
		}
		if (_bt < 0.0 || _bt > MAX_BT) {
			throw std::invalid_argument("_bt should be positive and not exceed " + std::to_string(MAX_BT));
		}
	}
};

} // namespace legacy
