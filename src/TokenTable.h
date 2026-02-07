#pragma once

#include <unordered_map>
#include "TokenKind.h"

// Single source of truth for all fixed token spellings.
// Both the consteval _tok literal and the runtime spell_to_kind()
// derive from this table.

struct TokenSpelling {
	std::string_view spelling;
	TokenKind        kind;
};

inline constexpr TokenSpelling all_fixed_tokens[] = {
	// ---- Punctuators ----
	{ "{",   tok::LBrace },    { "}",   tok::RBrace },
	{ "(",   tok::LParen },    { ")",   tok::RParen },
	{ "[",   tok::LBracket },  { "]",   tok::RBracket },
	{ ";",   tok::Semi },      { ",",   tok::Comma },
	{ ":",   tok::Colon },     { "::",  tok::ColonColon },
	{ "...", tok::Ellipsis },  { ".",   tok::Dot },
	{ "->",  tok::Arrow },     { "#",   tok::Hash },

	// ---- Operators ----
	{ "+",   tok::Plus },      { "-",   tok::Minus },
	{ "*",   tok::Star },      { "/",   tok::Slash },
	{ "%",   tok::Percent },   { "=",   tok::Assign },
	{ "==",  tok::Equal },     { "!=",  tok::NotEqual },
	{ "<",   tok::Less },      { ">",   tok::Greater },
	{ "<=",  tok::LessEq },    { ">=",  tok::GreaterEq },
	{ "<=>", tok::Spaceship },
	{ "&&",  tok::LogicalAnd },    { "||",  tok::LogicalOr },
	{ "!",   tok::LogicalNot },    { "&",   tok::BitwiseAnd },
	{ "|",   tok::BitwiseOr },     { "^",   tok::BitwiseXor },
	{ "~",   tok::BitwiseNot },
	{ "+=",  tok::PlusEq },    { "-=",  tok::MinusEq },
	{ "*=",  tok::StarEq },    { "/=",  tok::SlashEq },
	{ "%=",  tok::PercentEq },
	{ "&=",  tok::BitwiseAndAssign },  { "|=",  tok::BitwiseOrAssign },
	{ "^=",  tok::BitwiseXorAssign },
	{ "<<",  tok::ShiftLeft },   { ">>",  tok::ShiftRight },
	{ "<<=", tok::ShiftLeftAssign },   { ">>=", tok::ShiftRightAssign },
	{ "++",  tok::Increment },   { "--",  tok::Decrement },
	{ "?",   tok::Question },
	{ ".*",  tok::MemberPointer },
	{ "->*", tok::ArrowMemberPointer },

	// ---- Alternative operator spellings (same TokenKind) ----
	{ "and",    tok::LogicalAnd },     { "or",     tok::LogicalOr },
	{ "not",    tok::LogicalNot },     { "bitand", tok::BitwiseAnd },
	{ "bitor",  tok::BitwiseOr },      { "xor",    tok::BitwiseXor },
	{ "compl",  tok::BitwiseNot },     { "not_eq", tok::NotEqual },
	{ "and_eq", tok::BitwiseAndAssign },
	{ "or_eq",  tok::BitwiseOrAssign },
	{ "xor_eq", tok::BitwiseXorAssign },

	// ---- Keywords ----
	{ "if",             tok::KW_if },
	{ "else",           tok::KW_else },
	{ "while",          tok::KW_while },
	{ "for",            tok::KW_for },
	{ "do",             tok::KW_do },
	{ "return",         tok::KW_return },
	{ "class",          tok::KW_class },
	{ "struct",         tok::KW_struct },
	{ "enum",           tok::KW_enum },
	{ "union",          tok::KW_union },
	{ "namespace",      tok::KW_namespace },
	{ "template",       tok::KW_template },
	{ "typename",       tok::KW_typename },
	{ "typedef",        tok::KW_typedef },
	{ "using",          tok::KW_using },
	{ "const",          tok::KW_const },
	{ "static",         tok::KW_static },
	{ "virtual",        tok::KW_virtual },
	{ "override",       tok::KW_override },
	{ "final",          tok::KW_final },
	{ "public",         tok::KW_public },
	{ "private",        tok::KW_private },
	{ "protected",      tok::KW_protected },
	{ "friend",         tok::KW_friend },
	{ "void",           tok::KW_void },
	{ "int",            tok::KW_int },
	{ "auto",           tok::KW_auto },
	{ "switch",         tok::KW_switch },
	{ "case",           tok::KW_case },
	{ "default",        tok::KW_default },
	{ "break",          tok::KW_break },
	{ "continue",       tok::KW_continue },
	{ "new",            tok::KW_new },
	{ "delete",         tok::KW_delete },
	{ "try",            tok::KW_try },
	{ "catch",          tok::KW_catch },
	{ "throw",          tok::KW_throw },
	{ "sizeof",         tok::KW_sizeof },
	{ "constexpr",      tok::KW_constexpr },
	{ "consteval",      tok::KW_consteval },
	{ "constinit",      tok::KW_constinit },
	{ "static_cast",    tok::KW_static_cast },
	{ "dynamic_cast",   tok::KW_dynamic_cast },
	{ "const_cast",     tok::KW_const_cast },
	{ "reinterpret_cast", tok::KW_reinterpret_cast },
	{ "alignas",        tok::KW_alignas },
	{ "alignof",        tok::KW_alignof },
	{ "asm",            tok::KW_asm },
	{ "bool",           tok::KW_bool },
	{ "char",           tok::KW_char },
	{ "char8_t",        tok::KW_char8_t },
	{ "char16_t",       tok::KW_char16_t },
	{ "char32_t",       tok::KW_char32_t },
	{ "concept",        tok::KW_concept },
	{ "decltype",       tok::KW_decltype },
	{ "double",         tok::KW_double },
	{ "explicit",       tok::KW_explicit },
	{ "export",         tok::KW_export },
	{ "extern",         tok::KW_extern },
	{ "false",          tok::KW_false },
	{ "float",          tok::KW_float },
	{ "goto",           tok::KW_goto },
	{ "inline",         tok::KW_inline },
	{ "long",           tok::KW_long },
	{ "mutable",        tok::KW_mutable },
	{ "noexcept",       tok::KW_noexcept },
	{ "nullptr",        tok::KW_nullptr },
	{ "operator",       tok::KW_operator },
	{ "register",       tok::KW_register },
	{ "requires",       tok::KW_requires },
	{ "short",          tok::KW_short },
	{ "signed",         tok::KW_signed },
	{ "static_assert",  tok::KW_static_assert },
	{ "this",           tok::KW_this },
	{ "thread_local",   tok::KW_thread_local },
	{ "true",           tok::KW_true },
	{ "typeid",         tok::KW_typeid },
	{ "unsigned",       tok::KW_unsigned },
	{ "volatile",       tok::KW_volatile },
	{ "wchar_t",        tok::KW_wchar_t },
	// Microsoft-specific keywords
	{ "__int8",         tok::KW___int8 },
	{ "__int16",        tok::KW___int16 },
	{ "__int32",        tok::KW___int32 },
	{ "__int64",        tok::KW___int64 },
	{ "__ptr32",        tok::KW___ptr32 },
	{ "__ptr64",        tok::KW___ptr64 },
	{ "__w64",          tok::KW___w64 },
	{ "__unaligned",    tok::KW___unaligned },
	{ "__uptr",         tok::KW___uptr },
	{ "__sptr",         tok::KW___sptr },
	{ "__inline",       tok::KW___inline },
	{ "__forceinline",  tok::KW___forceinline },
	{ "__declspec",     tok::KW___declspec },
};

// Compile-time lookup: _tok user-defined literal
// Resolves at compile time; unrecognized strings are compile errors.
consteval TokenKind operator""_tok(const char* s, size_t len) {
	std::string_view sv(s, len);
	for (const auto& entry : all_fixed_tokens) {
		if (entry.spelling == sv) return entry.kind;
	}
	throw "unrecognized token literal";  // compile error
}

// Runtime lookup: convert a spelling string to its TokenKind.
// Uses a static hash map built once on first call for O(1) average lookup.
// Returns TokenKind::eof() if the spelling is not a fixed token.
inline TokenKind spell_to_kind(std::string_view spelling) {
	// Build hash map on first call (guaranteed thread-safe in C++11+)
	struct SpellMap {
		std::unordered_map<std::string_view, TokenKind> map;
		SpellMap() {
			map.reserve(sizeof(all_fixed_tokens) / sizeof(all_fixed_tokens[0]));
			for (const auto& entry : all_fixed_tokens) {
				map.emplace(entry.spelling, entry.kind);
			}
		}
	};
	static const SpellMap s_map;
	auto it = s_map.map.find(spelling);
	return it != s_map.map.end() ? it->second : TokenKind::eof();
}
