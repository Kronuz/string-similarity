// Independent reference implementations (textbook definitions, written for
// clarity and not speed) used as oracles by the tests. They share no code with
// the library.

#pragma once

#include <algorithm>
#include <cstddef>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace oracle {

// Textbook full-matrix weighted edit distance (raw, unnormalized).
inline size_t lev_raw(std::string_view a, std::string_view b, size_t sub, size_t indel) {
	std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1, 0));
	for (size_t i = 0; i <= a.size(); ++i) d[i][0] = i * indel;
	for (size_t j = 0; j <= b.size(); ++j) d[0][j] = j * indel;
	for (size_t i = 1; i <= a.size(); ++i) {
		for (size_t j = 1; j <= b.size(); ++j) {
			d[i][j] = std::min({d[i - 1][j] + indel, d[i][j - 1] + indel, d[i - 1][j - 1] + (a[i - 1] == b[j - 1] ? 0 : sub)});
		}
	}
	return d[a.size()][b.size()];
}

// The library normalizes by the largest single-operation cost times the longer length.
inline double lev_distance(std::string_view a, std::string_view b, size_t sub, size_t indel) {
	if (a.empty() || b.empty()) return 1.0;
	if (a == b) return 0.0;
	if (sub == 0 && indel == 0) return 0.0;  // everything is free
	return (double)lev_raw(a, b, sub, indel) / (std::max(sub, indel) * std::max(a.size(), b.size()));
}

struct JaroParts {
	size_t matches = 0;
	size_t mismatched = 0;  // matched characters that are out of order (before halving)
	double value = 0.0;
};

// The standard Jaro similarity: for each character of b in order, the first not
// yet matched equal character of a within floor(max/2)-1 positions (at least 0).
// Transpositions are half (integer division) the out-of-order matches.
inline JaroParts jaro(std::string_view a, std::string_view b) {
	JaroParts r;
	if (a.empty() || b.empty()) return r;
	const long half = (long)std::max(a.size(), b.size()) / 2;
	const long bound = half > 0 ? half - 1 : 0;
	std::vector<bool> used(a.size(), false);
	std::string from_b;
	for (long j = 0; j < (long)b.size(); ++j) {
		for (long i = std::max(0L, j - bound); i <= std::min((long)a.size() - 1, j + bound); ++i) {
			if (!used[i] && a[i] == b[j]) {
				used[i] = true;
				from_b += b[j];
				break;
			}
		}
	}
	std::string from_a;
	for (size_t i = 0; i < a.size(); ++i) if (used[i]) from_a += a[i];
	r.matches = from_b.size();
	if (!r.matches) return r;
	for (size_t k = 0; k < r.matches; ++k) r.mismatched += from_a[k] != from_b[k];
	const size_t t = r.mismatched / 2;
	const double m = (double)r.matches;
	r.value = (m / a.size() + m / b.size() + (m - t) / m) / 3.0;
	return r;
}

inline double jaro_winkler(std::string_view a, std::string_view b, double p = 0.1, double bt = 0.7) {
	const double j = jaro(a, b).value;
	if (!(j > bt)) return j;
	size_t l = 0;
	while (l < 4 && l < a.size() && l < b.size() && a[l] == b[l]) ++l;
	return j + l * p * (1.0 - j);
}

inline size_t lcs_subsequence(std::string_view a, std::string_view b) {
	std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1, 0));
	for (size_t i = 1; i <= a.size(); ++i)
		for (size_t j = 1; j <= b.size(); ++j)
			d[i][j] = a[i - 1] == b[j - 1] ? d[i - 1][j - 1] + 1 : std::max(d[i - 1][j], d[i][j - 1]);
	return d[a.size()][b.size()];
}

// Longest common substring by trying every start pair and extending.
inline size_t lcs_substring(std::string_view a, std::string_view b) {
	size_t best = 0;
	for (size_t i = 0; i < a.size(); ++i)
		for (size_t j = 0; j < b.size(); ++j) {
			size_t k = 0;
			while (i + k < a.size() && j + k < b.size() && a[i + k] == b[j + k]) ++k;
			best = std::max(best, k);
		}
	return best;
}

inline double jaccard(std::string_view a, std::string_view b) {
	std::set<char> sa(a.begin(), a.end()), sb(b.begin(), b.end()), u(sa);
	u.insert(sb.begin(), sb.end());
	size_t inter = 0;
	for (char c : sa) inter += sb.count(c);
	return (double)inter / u.size();
}

// Sorensen-Dice over bigram multisets: 2 * sum_g min(count_a(g), count_b(g)) / (|a|-1 + |b|-1).
inline double dice_multiset(std::string_view a, std::string_view b) {
	if (a.size() < 2 || b.size() < 2) return 0.0;
	std::map<std::string, int> ca, cb;
	for (size_t i = 0; i + 1 < a.size(); ++i) ++ca[std::string(a.substr(i, 2))];
	for (size_t i = 0; i + 1 < b.size(); ++i) ++cb[std::string(b.substr(i, 2))];
	size_t inter = 0;
	for (const auto& [g, n] : ca) { auto it = cb.find(g); if (it != cb.end()) inter += std::min(n, it->second); }
	return 2.0 * inter / ((a.size() - 1) + (b.size() - 1));
}

}  // namespace oracle
