#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

enum class TypeCategory : uint8_t;

namespace FlashCpp {

struct ParsedStringLiteralToken {
	std::string_view normalized_token;
	std::string_view content;
	bool is_raw = false;
	bool has_delimited_content = false;
};

size_t getLiteralEncodingPrefixLength(std::string_view literal_token);
TypeCategory getLiteralElementType(std::string_view literal_token);
ParsedStringLiteralToken parseStringLiteralToken(std::string_view token_raw);
unsigned parseHexDigit(char c);
std::string decodeStringLiteralBytes(std::string_view token_raw);
size_t computeStringLiteralContentLength(std::string_view token_raw);

}
