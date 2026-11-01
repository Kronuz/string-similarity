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
#include <bit>                                    // for std::countr_zero, std::popcount
#include <cstddef>                                // for std::size_t
#include <cstdint>                                // for std::uint32_t
#include <cstring>                                // for std::memset
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
	// that would be negative). The candidates are found with bit masks: the row of
	// the character (`peq`: a Peq or a DirectRows), minus the used bits, restricted to
	// the window, lowest set bit. The characters of str2 that matched are appended to
	// `out` in str2 order; `used` marks the matched positions of str1. Returns how
	// many matched.
	template <typename Rows>
	static size_t match_characters(const Rows& peq, strsim_detail::word_t* used, size_t l_str1, std::string_view str2, size_t bound, char* out) {
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

		// Per character, the first word of its row that can still hold a candidate: the
		// words before it are used up or lie left of every later window.
		std::uint32_t cursor[256];
		std::memset(cursor, 0, sizeof cursor);
		size_t lo = 0;  // words below lo are completely used
		for (const auto& c : str2) {
			const size_t start = j > bound ? j - bound : 0;
			const size_t end = std::min(j + bound + 1, l_str1);
			if (start < end) {
				const auto uc = static_cast<unsigned char>(c);
				const auto* row = peq.row(uc);
				while (lo < nb && ~used[lo] == 0) {
					++lo;
				}
				const size_t lastw = (end - 1) / W;
				size_t w = std::max({start / W, lo, static_cast<size_t>(cursor[uc])});
				for (; w <= lastw; ++w) {
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
				cursor[uc] = static_cast<std::uint32_t>(std::min(w, lastw));
			}
			++j;
		}
		return count;
	}

	// Both remainders of at most 64 characters: the used positions of str1 and the
	// matched positions of str2 are single words, kept in registers. The matched
	// characters of str2, in order, pair with the used positions of str1, in order;
	// `t` receives the transpositions. Returns how many characters matched.
	static size_t match_word(const unsigned char* p1, size_t r1, const unsigned char* p2, size_t r2, size_t bound, size_t& t) {
		using strsim_detail::word_t;
		// The mask of every byte of str1 (bit i: str1[i] is that byte): a thread-local
		// table that is zero between calls, cleared again below by walking str1.
		static thread_local word_t masks[256];
		for (size_t i = 0; i < r1; ++i) {
			masks[p1[i]] |= word_t(1) << i;
		}
		// The window of the j-th character of str2 is the positions [j - bound, j + bound]
		// of str1. It starts as [0, bound] and each step moves it up one position; while
		// its lower edge is still 0 the step also keeps position 0.
		word_t window = bound + 1 >= strsim_detail::kWordBits ? ~word_t(0) : (word_t(1) << (bound + 1)) - 1;
		word_t used = 0, matched = 0;
		// From j = r1 + bound on, no window reaches str1.
		const size_t end = std::min(r2, r1 + bound);
		size_t j = 0;
		for (const size_t grow = std::min(end, bound); j < grow; ++j) {
			const word_t m = masks[p2[j]] & window & ~used;
			used |= m & (~m + 1);  // lowest set bit, if any
			matched |= word_t(m != 0) << j;
			window = (window << 1) | 1;
		}
		for (; j < end; ++j) {
			const word_t m = masks[p2[j]] & window & ~used;
			used |= m & (~m + 1);
			matched |= word_t(m != 0) << j;
			window <<= 1;
		}
		for (size_t i = 0; i < r1; ++i) {
			masks[p1[i]] = 0;
		}

		size_t out_of_order = 0;
		for (word_t u = used, s = matched; s; u &= u - 1, s &= s - 1) {
			out_of_order += p1[std::countr_zero(u)] != p2[std::countr_zero(s)];
		}
		t = out_of_order / 2;
		return static_cast<size_t>(std::popcount(used));
	}

	// A remainder longer than 64 characters: the mask rows of str1 (up to 64 words,
	// kept in a direct table; longer ones in thread-local scratch), the used
	// positions in words, and the matched characters of str2 in order.
	static size_t match_blocks(const unsigned char* p1, size_t r1, const unsigned char* p2, size_t r2, size_t bound, size_t& t) {
		constexpr size_t kStack = 512;
		char stack_buf[kStack];
		std::string heap_buf;
		char* common = stack_buf;
		if (r2 > kStack) {
			heap_buf.resize(r2);
			common = heap_buf.data();
		}

		// common: str2's characters matched into str1, in str2 order; the used
		// positions of str1, in order, are the other half of the matching.
		const std::string_view text(reinterpret_cast<const char*>(p2), r2);
		const size_t nb1 = (r1 + 63) / 64;
		if (nb1 <= strsim_detail::DirectRows::kMaxBlocks) {
			strsim_detail::word_t used[strsim_detail::DirectRows::kMaxBlocks];
			const strsim_detail::DirectRows rows(p1, r1);
			const size_t m = match_characters(rows, used, r1, text, bound, common);
			return transpositions(p1, used, nb1, common, m, t);
		}
		strsim_detail::Peq<0> peq1(p1, r1, nb1);
		auto* used = peq1.extra();
		const size_t m = match_characters(peq1, used, r1, text, bound, common);
		return transpositions(p1, used, nb1, common, m, t);
	}

	// Transpositions: half the matched characters that differ in order; `common` holds
	// the m matched characters of str2, the used positions of str1 pair with them in order.
	static size_t transpositions(const unsigned char* p1, const strsim_detail::word_t* used, size_t nb1, const char* common, size_t m, size_t& t) {
		size_t out_of_order = 0, k = 0;
		for (size_t w = 0; w < nb1; ++w) {
			for (auto bits = used[w]; bits; bits &= bits - 1) {
				out_of_order += p1[w * 64 + std::countr_zero(bits)] != static_cast<unsigned char>(common[k++]);
			}
		}
		t = out_of_order / 2;
		return m;
	}

public:
	// The Jaro similarity of two (already case-folded) strings.
	//
	// The common prefix is stripped first: it matches itself position by position
	// (the first unused equal character in the window of the j-th character is the
	// j-th one, since every earlier position is taken), it holds no transposition and
	// it takes the same positions out of every window, so the rest of the strings is
	// matched on its own with the window bound of the full lengths. The rest of two
	// strings of up to 64 characters is matched in registers; longer ones use mask rows.
	static double jaro(std::string_view str1, std::string_view str2) {
		const auto l_str1 = str1.size(), l_str2 = str2.size();
		if (!l_str1 || !l_str2) {
			return 0.0;
		}

		const size_t half = std::max(l_str1, l_str2) / 2;
		const size_t bound = half ? half - 1 : 0;

		const auto* p1 = reinterpret_cast<const unsigned char*>(str1.data());
		const auto* p2 = reinterpret_cast<const unsigned char*>(str2.data());
		const size_t prefix = strsim_detail::common_prefix(p1, p2, std::min(l_str1, l_str2));
		const size_t r1 = l_str1 - prefix, r2 = l_str2 - prefix;

		size_t m = prefix, t = 0;
		if (r1 && r2) {
			p1 += prefix;
			p2 += prefix;
			size_t matched = 0;
			if (r1 <= 64 && r2 <= 64) {
				matched = match_word(p1, r1, p2, r2, bound, t);
			} else {
				matched = match_blocks(p1, r1, p2, r2, bound, t);
			}
			m += matched;
		}
		if (!m) {
			return 0.0;
		}

		return (((double)m / l_str1) + ((double)m / l_str2) + ((double)(m - t) / m)) / 3.0;
	}

protected:
	double _similarity(std::string_view str1, std::string_view str2) const {
		return jaro(str1, str2);
	}

	// Case folding happens here, on the fly, rather than in the base class.
	double _icase_similarity(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		strsim_detail::Folded f1(str1, fold_first), f2(str2, fold_second);
		return _similarity(f1.view(), f2.view());
	}

	double _icase_distance(std::string_view str1, std::string_view str2, bool fold_first, bool fold_second) const {
		return 1.0 - _icase_similarity(str1, str2, fold_first, fold_second);
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
