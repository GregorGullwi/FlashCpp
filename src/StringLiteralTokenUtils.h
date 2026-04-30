#pragma once

#include <cctype>
#include <string>
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

inline unsigned parseHexDigit(char c) {
	if (c >= '0' && c <= '9') {
		return static_cast<unsigned>(c - '0');
	}
	if (c >= 'a' && c <= 'f') {
		return static_cast<unsigned>(10 + (c - 'a'));
	}
	if (c >= 'A' && c <= 'F') {
		return static_cast<unsigned>(10 + (c - 'A'));
	}
	return 0;
}

inline std::string decodeStringLiteralBytes(std::string_view token_raw) {
	const auto parsed_literal = parseStringLiteralToken(token_raw);
	const bool process_escapes = !parsed_literal.is_raw;
	std::string_view content =
		parsed_literal.is_raw && !parsed_literal.has_delimited_content
		? parsed_literal.normalized_token
		: parsed_literal.content;

	std::string decoded;
	decoded.reserve(content.size());

	for (size_t i = 0; i < content.size(); ++i) {
		char c = content[i];
		if (!process_escapes || c != '\\' || i + 1 >= content.size()) {
			decoded.push_back(c);
			continue;
		}

		++i;
		const char esc = content[i];
		switch (esc) {
		case 'n':
			decoded.push_back('\n');
			break;
		case 't':
			decoded.push_back('\t');
			break;
		case 'r':
			decoded.push_back('\r');
			break;
		case '\\':
			decoded.push_back('\\');
			break;
		case '"':
			decoded.push_back('"');
			break;
		case '\'':
			decoded.push_back('\'');
			break;
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7': {
			unsigned value = static_cast<unsigned>(esc - '0');
			for (int d = 0; d < 2 && i + 1 < content.size() &&
				 content[i + 1] >= '0' && content[i + 1] <= '7'; ++d) {
				++i;
				value = (value * 8) + static_cast<unsigned>(content[i] - '0');
			}
			decoded.push_back(static_cast<char>(value & 0xFFu));
			break;
		}
		case 'x': {
			unsigned value = 0;
			bool saw_digit = false;
			while (i + 1 < content.size() &&
				   std::isxdigit(static_cast<unsigned char>(content[i + 1]))) {
				++i;
				saw_digit = true;
				value = (value * 16) + parseHexDigit(content[i]);
			}
			decoded.push_back(static_cast<char>((saw_digit ? value : static_cast<unsigned>('x')) & 0xFFu));
			break;
		}
		case 'u':
		case 'U': {
			const int digits = (esc == 'u') ? 4 : 8;
			unsigned value = 0;
			int consumed = 0;
			while (consumed < digits && i + 1 < content.size() &&
				   std::isxdigit(static_cast<unsigned char>(content[i + 1]))) {
				++i;
				++consumed;
				value = (value * 16) + parseHexDigit(content[i]);
			}
			decoded.push_back(static_cast<char>(value & 0xFFu));
			break;
		}
		case 'a':
			decoded.push_back('\a');
			break;
		case 'b':
			decoded.push_back('\b');
			break;
		case 'f':
			decoded.push_back('\f');
			break;
		case 'v':
			decoded.push_back('\v');
			break;
		default:
			decoded.push_back(esc);
			break;
		}
	}

	return decoded;
}

inline size_t computeStringLiteralContentLength(std::string_view token_raw) {
	const auto parsed_literal = parseStringLiteralToken(token_raw);
	if (parsed_literal.is_raw) {
		return parsed_literal.has_delimited_content ? parsed_literal.content.size() : 0;
	}

	if (!parsed_literal.has_delimited_content) {
		return 0;
	}

	std::string_view content = parsed_literal.content;
	size_t len = 0;
	for (size_t i = 0; i < content.size(); ++i) {
		if (content[i] == '\\' && i + 1 < content.size()) {
			++i;
			const char esc = content[i];
			if (esc == 'x') {
				while (i + 1 < content.size() &&
					   std::isxdigit(static_cast<unsigned char>(content[i + 1])))
					++i;
			} else if (esc >= '0' && esc <= '7') {
				for (int d = 0; d < 2 && i + 1 < content.size() &&
					 content[i + 1] >= '0' && content[i + 1] <= '7'; ++d)
					++i;
			} else if (esc == 'u') {
				for (int d = 0; d < 4 && i + 1 < content.size() &&
					 std::isxdigit(static_cast<unsigned char>(content[i + 1])); ++d)
					++i;
			} else if (esc == 'U') {
				for (int d = 0; d < 8 && i + 1 < content.size() &&
					 std::isxdigit(static_cast<unsigned char>(content[i + 1])); ++d)
					++i;
			}
		}
		++len;
	}
	return len;
}

}
