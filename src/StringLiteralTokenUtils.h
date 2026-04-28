#pragma once

#include <string_view>

namespace FlashCpp {

struct ParsedStringLiteralToken {
	std::string_view normalized_token;
	std::string_view content;
	bool is_raw = false;
	bool has_delimited_content = false;
};

inline ParsedStringLiteralToken parseStringLiteralToken(std::string_view token_raw) {
	ParsedStringLiteralToken result{
		.normalized_token = token_raw,
		.content = token_raw,
	};
	if (token_raw.empty()) {
		return result;
	}

	std::string_view token = token_raw;
	if (token.front() != '"' && token.front() != 'R') {
		const size_t quote = token.find('"');
		const size_t raw_marker = token.find('R');
		if (raw_marker != std::string_view::npos &&
			(quote == std::string_view::npos || raw_marker < quote)) {
			token = token.substr(raw_marker);
		} else if (quote != std::string_view::npos) {
			token = token.substr(quote);
		} else {
			return result;
		}
	}

	result.normalized_token = token;
	result.content = token;
	if (token.empty()) {
		return result;
	}

	if (token.front() == 'R') {
		result.is_raw = true;
		if (token.size() < 3 || token[1] != '"') {
			return result;
		}

		const size_t open_paren = token.find('(', 2);
		if (open_paren == std::string_view::npos || token.back() != '"') {
			return result;
		}

		const std::string_view delimiter = token.substr(2, open_paren - 2);
		const size_t closing_marker_size = delimiter.size() + 2; // )delim"
		if (token.size() < open_paren + 1 + closing_marker_size) {
			return result;
		}

		const size_t close_paren = token.size() - closing_marker_size;
		if (token[close_paren] != ')' ||
			token.substr(close_paren + 1, delimiter.size()) != delimiter) {
			return result;
		}

		result.content = token.substr(open_paren + 1, close_paren - open_paren - 1);
		result.has_delimited_content = true;
		return result;
	}

	if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
		result.content = token.substr(1, token.size() - 2);
		result.has_delimited_content = true;
	}

	return result;
}

}
