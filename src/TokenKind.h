#pragma once

#include <cstdint>
#include <string_view>

// ---- Per-category ID enums (auto-incremented) ----

enum class PunctId : uint16_t {
	LBrace, RBrace, LParen, RParen, LBracket, RBracket,
	Semi, Comma, Colon, ColonColon, Ellipsis, Dot, Arrow, Hash
};

enum class OpId : uint16_t {
	Plus, Minus, Star, Slash, Percent, Assign,
	Equal, NotEqual, Less, Greater, LessEq, GreaterEq, Spaceship,
	LogicalAnd,       // && / and
	LogicalOr,        // || / or
	LogicalNot,       // !  / not
	BitwiseAnd,       // &  / bitand
	BitwiseOr,        // |  / bitor
	BitwiseXor,       // ^  / xor
	BitwiseNot,       // ~  / compl
	PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
	BitwiseAndAssign, // &= / and_eq
	BitwiseOrAssign,  // |= / or_eq
	BitwiseXorAssign, // ^= / xor_eq
	ShiftLeft, ShiftRight, ShiftLeftAssign, ShiftRightAssign,
	Increment, Decrement, Question, MemberPointer, ArrowMemberPointer
};

enum class KeywordId : uint16_t {
	If, Else, While, For, Do, Return,
	Class, Struct, Enum, Union, Namespace,
	Template, Typename, Typedef, Using,
	Const, Static, Virtual, Override, Final,
	Public, Private, Protected, Friend,
	Void, Int, Auto,
	Switch, Case, Default, Break, Continue,
	New, Delete, Try, Catch, Throw,
	Sizeof, Constexpr, Consteval, Constinit,
	StaticCast, DynamicCast, ConstCast, ReinterpretCast,
	// Additional C++ keywords
	Alignas, Alignof, Asm, Bool, Char, Char8_t, Char16_t, Char32_t,
	Concept, Decltype, Double, Explicit, Export, Extern,
	False, Float, Goto, Inline, Long, Mutable,
	Noexcept, Nullptr, Operator, Register, Requires,
	Short, Signed, StaticAssert, This, ThreadLocal,
	True, Typeid, Unsigned, Volatile, Wchar_t,
	// Microsoft-specific keywords
	MSVC_Int8, MSVC_Int16, MSVC_Int32, MSVC_Int64,
	MSVC_Ptr32, MSVC_Ptr64, MSVC_W64, MSVC_Unaligned,
	MSVC_Uptr, MSVC_Sptr,
	MSVC_Inline, MSVC_Forceinline,
	MSVC_Declspec,
};

class TokenKind {
public:
	// Categories (upper 8 bits)
	enum class Category : uint8_t {
		None = 0,       // EOF / uninitialized
		Identifier,     // user identifiers
		Keyword,        // language keywords
		Literal,        // numeric literals
		StringLiteral,
		CharLiteral,
		Operator,       // + - * / == != || && ...
		Punctuator,     // { } ( ) [ ] ; , : :: ...
	};

	constexpr TokenKind() = default;

	constexpr TokenKind(Category cat, uint16_t id)
		: category_(cat), id_(id) {}

	constexpr Category category()  const { return category_; }
	constexpr uint16_t id()        const { return id_; }

	constexpr bool operator==(TokenKind o) const {
		return category_ == o.category_ && id_ == o.id_;
	}
	constexpr bool operator!=(TokenKind o) const { return !(*this == o); }

	// Special sentinels
	static constexpr TokenKind eof()   { return {}; }
	static constexpr TokenKind ident() {
		return { Category::Identifier, 0 };
	}
	static constexpr TokenKind literal() {
		return { Category::Literal, 0 };
	}
	static constexpr TokenKind string_literal() {
		return { Category::StringLiteral, 0 };
	}
	static constexpr TokenKind char_literal() {
		return { Category::CharLiteral, 0 };
	}

	constexpr bool is_eof()            const { return category_ == Category::None; }
	constexpr bool is_identifier()     const { return category_ == Category::Identifier; }
	constexpr bool is_keyword()        const { return category_ == Category::Keyword; }
	constexpr bool is_literal()        const { return category_ == Category::Literal; }
	constexpr bool is_string_literal() const { return category_ == Category::StringLiteral; }
	constexpr bool is_char_literal()   const { return category_ == Category::CharLiteral; }
	constexpr bool is_operator()       const { return category_ == Category::Operator; }
	constexpr bool is_punctuator()     const { return category_ == Category::Punctuator; }

	// Typed factories — category is implicit from the enum type
	static constexpr TokenKind punct(PunctId id) {
		return { Category::Punctuator, static_cast<uint16_t>(id) };
	}
	static constexpr TokenKind op(OpId id) {
		return { Category::Operator, static_cast<uint16_t>(id) };
	}
	static constexpr TokenKind kw(KeywordId id) {
		return { Category::Keyword, static_cast<uint16_t>(id) };
	}

private:
	Category category_ = Category::None;
	uint16_t id_ = 0;       // unique within category
	// Total: 4 bytes — fits in a single register
};

namespace tok {
	// Punctuators
	inline constexpr auto LBrace     = TokenKind::punct(PunctId::LBrace);
	inline constexpr auto RBrace     = TokenKind::punct(PunctId::RBrace);
	inline constexpr auto LParen     = TokenKind::punct(PunctId::LParen);
	inline constexpr auto RParen     = TokenKind::punct(PunctId::RParen);
	inline constexpr auto LBracket   = TokenKind::punct(PunctId::LBracket);
	inline constexpr auto RBracket   = TokenKind::punct(PunctId::RBracket);
	inline constexpr auto Semi       = TokenKind::punct(PunctId::Semi);
	inline constexpr auto Comma      = TokenKind::punct(PunctId::Comma);
	inline constexpr auto Colon      = TokenKind::punct(PunctId::Colon);
	inline constexpr auto ColonColon = TokenKind::punct(PunctId::ColonColon);
	inline constexpr auto Ellipsis   = TokenKind::punct(PunctId::Ellipsis);
	inline constexpr auto Dot        = TokenKind::punct(PunctId::Dot);
	inline constexpr auto Arrow      = TokenKind::punct(PunctId::Arrow);
	inline constexpr auto Hash       = TokenKind::punct(PunctId::Hash);

	// Operators
	inline constexpr auto Plus       = TokenKind::op(OpId::Plus);
	inline constexpr auto Minus      = TokenKind::op(OpId::Minus);
	inline constexpr auto Star       = TokenKind::op(OpId::Star);
	inline constexpr auto Slash      = TokenKind::op(OpId::Slash);
	inline constexpr auto Percent    = TokenKind::op(OpId::Percent);
	inline constexpr auto Assign     = TokenKind::op(OpId::Assign);
	inline constexpr auto Equal      = TokenKind::op(OpId::Equal);
	inline constexpr auto NotEqual   = TokenKind::op(OpId::NotEqual);
	inline constexpr auto Less       = TokenKind::op(OpId::Less);
	inline constexpr auto Greater    = TokenKind::op(OpId::Greater);
	inline constexpr auto LessEq     = TokenKind::op(OpId::LessEq);
	inline constexpr auto GreaterEq  = TokenKind::op(OpId::GreaterEq);
	inline constexpr auto Spaceship  = TokenKind::op(OpId::Spaceship);
	inline constexpr auto LogicalAnd = TokenKind::op(OpId::LogicalAnd);
	inline constexpr auto LogicalOr  = TokenKind::op(OpId::LogicalOr);
	inline constexpr auto LogicalNot = TokenKind::op(OpId::LogicalNot);
	inline constexpr auto BitwiseAnd = TokenKind::op(OpId::BitwiseAnd);
	inline constexpr auto BitwiseOr  = TokenKind::op(OpId::BitwiseOr);
	inline constexpr auto BitwiseXor = TokenKind::op(OpId::BitwiseXor);
	inline constexpr auto BitwiseNot = TokenKind::op(OpId::BitwiseNot);
	inline constexpr auto PlusEq     = TokenKind::op(OpId::PlusEq);
	inline constexpr auto MinusEq    = TokenKind::op(OpId::MinusEq);
	inline constexpr auto StarEq     = TokenKind::op(OpId::StarEq);
	inline constexpr auto SlashEq    = TokenKind::op(OpId::SlashEq);
	inline constexpr auto PercentEq  = TokenKind::op(OpId::PercentEq);
	inline constexpr auto BitwiseAndAssign = TokenKind::op(OpId::BitwiseAndAssign);
	inline constexpr auto BitwiseOrAssign  = TokenKind::op(OpId::BitwiseOrAssign);
	inline constexpr auto BitwiseXorAssign = TokenKind::op(OpId::BitwiseXorAssign);
	inline constexpr auto ShiftLeft        = TokenKind::op(OpId::ShiftLeft);
	inline constexpr auto ShiftRight       = TokenKind::op(OpId::ShiftRight);
	inline constexpr auto ShiftLeftAssign  = TokenKind::op(OpId::ShiftLeftAssign);
	inline constexpr auto ShiftRightAssign = TokenKind::op(OpId::ShiftRightAssign);
	inline constexpr auto Increment  = TokenKind::op(OpId::Increment);
	inline constexpr auto Decrement  = TokenKind::op(OpId::Decrement);
	inline constexpr auto Question   = TokenKind::op(OpId::Question);
	inline constexpr auto MemberPointer      = TokenKind::op(OpId::MemberPointer);
	inline constexpr auto ArrowMemberPointer = TokenKind::op(OpId::ArrowMemberPointer);

	// Keywords
	inline constexpr auto KW_if           = TokenKind::kw(KeywordId::If);
	inline constexpr auto KW_else         = TokenKind::kw(KeywordId::Else);
	inline constexpr auto KW_while        = TokenKind::kw(KeywordId::While);
	inline constexpr auto KW_for          = TokenKind::kw(KeywordId::For);
	inline constexpr auto KW_do           = TokenKind::kw(KeywordId::Do);
	inline constexpr auto KW_return       = TokenKind::kw(KeywordId::Return);
	inline constexpr auto KW_class        = TokenKind::kw(KeywordId::Class);
	inline constexpr auto KW_struct       = TokenKind::kw(KeywordId::Struct);
	inline constexpr auto KW_enum         = TokenKind::kw(KeywordId::Enum);
	inline constexpr auto KW_union        = TokenKind::kw(KeywordId::Union);
	inline constexpr auto KW_namespace    = TokenKind::kw(KeywordId::Namespace);
	inline constexpr auto KW_template     = TokenKind::kw(KeywordId::Template);
	inline constexpr auto KW_typename     = TokenKind::kw(KeywordId::Typename);
	inline constexpr auto KW_typedef      = TokenKind::kw(KeywordId::Typedef);
	inline constexpr auto KW_using        = TokenKind::kw(KeywordId::Using);
	inline constexpr auto KW_const        = TokenKind::kw(KeywordId::Const);
	inline constexpr auto KW_static       = TokenKind::kw(KeywordId::Static);
	inline constexpr auto KW_virtual      = TokenKind::kw(KeywordId::Virtual);
	inline constexpr auto KW_override     = TokenKind::kw(KeywordId::Override);
	inline constexpr auto KW_final        = TokenKind::kw(KeywordId::Final);
	inline constexpr auto KW_public       = TokenKind::kw(KeywordId::Public);
	inline constexpr auto KW_private      = TokenKind::kw(KeywordId::Private);
	inline constexpr auto KW_protected    = TokenKind::kw(KeywordId::Protected);
	inline constexpr auto KW_friend       = TokenKind::kw(KeywordId::Friend);
	inline constexpr auto KW_void         = TokenKind::kw(KeywordId::Void);
	inline constexpr auto KW_int          = TokenKind::kw(KeywordId::Int);
	inline constexpr auto KW_auto         = TokenKind::kw(KeywordId::Auto);
	inline constexpr auto KW_switch       = TokenKind::kw(KeywordId::Switch);
	inline constexpr auto KW_case         = TokenKind::kw(KeywordId::Case);
	inline constexpr auto KW_default      = TokenKind::kw(KeywordId::Default);
	inline constexpr auto KW_break        = TokenKind::kw(KeywordId::Break);
	inline constexpr auto KW_continue     = TokenKind::kw(KeywordId::Continue);
	inline constexpr auto KW_new          = TokenKind::kw(KeywordId::New);
	inline constexpr auto KW_delete       = TokenKind::kw(KeywordId::Delete);
	inline constexpr auto KW_try          = TokenKind::kw(KeywordId::Try);
	inline constexpr auto KW_catch        = TokenKind::kw(KeywordId::Catch);
	inline constexpr auto KW_throw        = TokenKind::kw(KeywordId::Throw);
	inline constexpr auto KW_sizeof       = TokenKind::kw(KeywordId::Sizeof);
	inline constexpr auto KW_constexpr    = TokenKind::kw(KeywordId::Constexpr);
	inline constexpr auto KW_consteval    = TokenKind::kw(KeywordId::Consteval);
	inline constexpr auto KW_constinit    = TokenKind::kw(KeywordId::Constinit);
	inline constexpr auto KW_static_cast  = TokenKind::kw(KeywordId::StaticCast);
	inline constexpr auto KW_dynamic_cast = TokenKind::kw(KeywordId::DynamicCast);
	inline constexpr auto KW_const_cast   = TokenKind::kw(KeywordId::ConstCast);
	inline constexpr auto KW_reinterpret_cast = TokenKind::kw(KeywordId::ReinterpretCast);
	// Additional C++ keywords
	inline constexpr auto KW_alignas      = TokenKind::kw(KeywordId::Alignas);
	inline constexpr auto KW_alignof      = TokenKind::kw(KeywordId::Alignof);
	inline constexpr auto KW_asm          = TokenKind::kw(KeywordId::Asm);
	inline constexpr auto KW_bool         = TokenKind::kw(KeywordId::Bool);
	inline constexpr auto KW_char         = TokenKind::kw(KeywordId::Char);
	inline constexpr auto KW_char8_t      = TokenKind::kw(KeywordId::Char8_t);
	inline constexpr auto KW_char16_t     = TokenKind::kw(KeywordId::Char16_t);
	inline constexpr auto KW_char32_t     = TokenKind::kw(KeywordId::Char32_t);
	inline constexpr auto KW_concept      = TokenKind::kw(KeywordId::Concept);
	inline constexpr auto KW_decltype     = TokenKind::kw(KeywordId::Decltype);
	inline constexpr auto KW_double       = TokenKind::kw(KeywordId::Double);
	inline constexpr auto KW_explicit     = TokenKind::kw(KeywordId::Explicit);
	inline constexpr auto KW_export       = TokenKind::kw(KeywordId::Export);
	inline constexpr auto KW_extern       = TokenKind::kw(KeywordId::Extern);
	inline constexpr auto KW_false        = TokenKind::kw(KeywordId::False);
	inline constexpr auto KW_float        = TokenKind::kw(KeywordId::Float);
	inline constexpr auto KW_goto         = TokenKind::kw(KeywordId::Goto);
	inline constexpr auto KW_inline       = TokenKind::kw(KeywordId::Inline);
	inline constexpr auto KW_long         = TokenKind::kw(KeywordId::Long);
	inline constexpr auto KW_mutable      = TokenKind::kw(KeywordId::Mutable);
	inline constexpr auto KW_noexcept     = TokenKind::kw(KeywordId::Noexcept);
	inline constexpr auto KW_nullptr      = TokenKind::kw(KeywordId::Nullptr);
	inline constexpr auto KW_operator     = TokenKind::kw(KeywordId::Operator);
	inline constexpr auto KW_register     = TokenKind::kw(KeywordId::Register);
	inline constexpr auto KW_requires     = TokenKind::kw(KeywordId::Requires);
	inline constexpr auto KW_short        = TokenKind::kw(KeywordId::Short);
	inline constexpr auto KW_signed       = TokenKind::kw(KeywordId::Signed);
	inline constexpr auto KW_static_assert = TokenKind::kw(KeywordId::StaticAssert);
	inline constexpr auto KW_this         = TokenKind::kw(KeywordId::This);
	inline constexpr auto KW_thread_local = TokenKind::kw(KeywordId::ThreadLocal);
	inline constexpr auto KW_true         = TokenKind::kw(KeywordId::True);
	inline constexpr auto KW_typeid       = TokenKind::kw(KeywordId::Typeid);
	inline constexpr auto KW_unsigned     = TokenKind::kw(KeywordId::Unsigned);
	inline constexpr auto KW_volatile     = TokenKind::kw(KeywordId::Volatile);
	inline constexpr auto KW_wchar_t      = TokenKind::kw(KeywordId::Wchar_t);
	// Microsoft-specific keywords
	inline constexpr auto KW___int8       = TokenKind::kw(KeywordId::MSVC_Int8);
	inline constexpr auto KW___int16      = TokenKind::kw(KeywordId::MSVC_Int16);
	inline constexpr auto KW___int32      = TokenKind::kw(KeywordId::MSVC_Int32);
	inline constexpr auto KW___int64      = TokenKind::kw(KeywordId::MSVC_Int64);
	inline constexpr auto KW___ptr32      = TokenKind::kw(KeywordId::MSVC_Ptr32);
	inline constexpr auto KW___ptr64      = TokenKind::kw(KeywordId::MSVC_Ptr64);
	inline constexpr auto KW___w64        = TokenKind::kw(KeywordId::MSVC_W64);
	inline constexpr auto KW___unaligned  = TokenKind::kw(KeywordId::MSVC_Unaligned);
	inline constexpr auto KW___uptr       = TokenKind::kw(KeywordId::MSVC_Uptr);
	inline constexpr auto KW___sptr       = TokenKind::kw(KeywordId::MSVC_Sptr);
	inline constexpr auto KW___inline     = TokenKind::kw(KeywordId::MSVC_Inline);
	inline constexpr auto KW___forceinline = TokenKind::kw(KeywordId::MSVC_Forceinline);
	inline constexpr auto KW___declspec   = TokenKind::kw(KeywordId::MSVC_Declspec);
} // namespace tok
