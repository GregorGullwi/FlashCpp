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
		token.remove_prefix(1);
		if (token.size() < 2 || token.front() != '"' || token.back() != '"') {
			return result;
		}

		token = token.substr(1, token.size() - 2);
		const size_t open_paren = token.find('(');
		const size_t close_paren = token.rfind(')');
		if (open_paren == std::string_view::npos ||
			close_paren == std::string_view::npos ||
			close_paren <= open_paren) {
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
