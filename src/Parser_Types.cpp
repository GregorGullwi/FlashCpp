// Helper function to get Type and size for built-in type keywords
// Used by both parse_type_specifier and functional-style cast parsing
std::optional<std::pair<Type, unsigned char>> Parser::get_builtin_type_info(std::string_view type_name) {
	// Most types have fixed sizes, but 'long' size depends on target data model
	static const std::unordered_map<std::string_view, std::pair<Type, unsigned char>> builtin_types = {
		{"void", {Type::Void, 0}},
		{"bool", {Type::Bool, 8}},
		{"char", {Type::Char, 8}},
		// Note: "wchar_t" is handled specially below due to target-dependent size (16 on Windows, 32 on Linux)
		{"char8_t", {Type::Char8, 8}},         // C++20 UTF-8 character type
		{"char16_t", {Type::Char16, 16}},      // C++11 UTF-16 character type
		{"char32_t", {Type::Char32, 32}},      // C++11 UTF-32 character type
		{"short", {Type::Short, 16}},
		{"int", {Type::Int, 32}},
		// Note: "long" is handled specially below due to target-dependent size
		{"float", {Type::Float, 32}},
		{"double", {Type::Double, 64}},
		{"__int8", {Type::Char, 8}},
		{"__int16", {Type::Short, 16}},
		{"__int32", {Type::Int, 32}},
		{"__int64", {Type::LongLong, 64}},
		{"signed", {Type::Int, 32}},  // signed without type defaults to int
		{"unsigned", {Type::UnsignedInt, 32}},  // unsigned without type defaults to unsigned int
	};

	// Handle "long" specially since its size depends on target data model
	// Windows (LLP64): long = 32 bits, Linux/Unix (LP64): long = 64 bits
	if (type_name == "long") {
		return std::make_pair(Type::Long, static_cast<unsigned char>(get_type_size_bits(Type::Long)));
	}

	// Handle "wchar_t" specially since its size depends on target
	// Windows (LLP64): wchar_t = 16 bits unsigned, Linux (LP64): wchar_t = 32 bits signed
	if (type_name == "wchar_t") {
		return std::make_pair(Type::WChar, static_cast<unsigned char>(get_wchar_size_bits()));
	}
	
	auto it = builtin_types.find(type_name);
	if (it != builtin_types.end()) {
		return it->second;
	}
	return std::nullopt;
}

// Helper function to parse functional-style cast: Type(expression) or Type() for value initialization
// This consolidates the logic for parsing functional casts from both keyword and identifier contexts
ParseResult Parser::parse_functional_cast(std::string_view type_name, const Token& type_token) {
	// Expect '(' after type name
	if (current_token_.kind().is_eof() || current_token_.value() != "(") {
		return ParseResult::error("Expected '(' for functional cast", type_token);
	}
	
	advance(); // consume '('
	
	// Get type information first (needed for both empty and non-empty cases)
	Type cast_type = Type::Int; // default
	TypeQualifier qualifier = TypeQualifier::None;
	int type_size = 32;
	
	auto builtin_type_info = get_builtin_type_info(type_name);
	if (builtin_type_info.has_value()) {
		cast_type = builtin_type_info->first;
		type_size = builtin_type_info->second;
		// Handle special case for unsigned qualifier
		if (type_name == "unsigned") {
			qualifier = TypeQualifier::Unsigned;
		}
	} else {
		// User-defined type - look it up
		StringHandle type_handle = StringTable::getOrInternStringHandle(type_name);
		auto type_it = gTypesByName.find(type_handle);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* type_info = type_it->second;
			cast_type = type_info->type_;
			type_size = type_info->type_size_;
			if (type_info->isStruct()) {
				cast_type = Type::Struct;
			}
		}
	}
	
	// Check for empty parentheses: Type() is value initialization (zero for scalar types)
	if (current_token_.value() == ")") {
		advance(); // consume ')'
		
		// Create a zero literal of the appropriate type (value initialization)
		Token zero_token(Token::Type::Literal, "0"sv, type_token.line(), type_token.column(), type_token.file_index());
		
		// Use 0.0 for floating point types, 0 for integral types
		if (cast_type == Type::Double || cast_type == Type::Float) {
			auto zero_expr = emplace_node<ExpressionNode>(
				NumericLiteralNode(zero_token, 0.0, cast_type, qualifier, type_size)
			);
			return ParseResult::success(zero_expr);
		} else {
			auto zero_expr = emplace_node<ExpressionNode>(
				NumericLiteralNode(zero_token, 0ULL, cast_type, qualifier, type_size)
			);
			return ParseResult::success(zero_expr);
		}
	}
	
	// Parse the expression inside the parentheses
	ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (expr_result.is_error()) {
		return expr_result;
	}
	
	// Check for pack expansion (...) after the expression
	// This handles patterns like: int(__args...) in decltype contexts
	std::optional<ASTNode> final_expr = expr_result.node();
	if (peek() == "..."_tok) {
		Token ellipsis_token = peek_info();
		advance(); // consume '...'
		
		// Wrap the expression in a PackExpansionExprNode
		if (final_expr.has_value()) {
			final_expr = emplace_node<ExpressionNode>(
				PackExpansionExprNode(*final_expr, ellipsis_token));
		}
	}
	
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after functional cast expression", current_token_);
	}
	
	auto type_node = emplace_node<TypeSpecifierNode>(cast_type, qualifier, type_size, type_token);
	
	// Create a static cast node (functional cast behaves like static_cast)
	auto result = emplace_node<ExpressionNode>(
		StaticCastNode(type_node, *final_expr, type_token));
	
	return ParseResult::success(result);
}

// Helper function to parse cv-qualifiers (const and/or volatile) from the token stream
// Consolidates the repeated pattern of parsing const/volatile keywords throughout Parser.cpp
// Returns combined CVQualifier flags: None, Const, Volatile, or ConstVolatile
CVQualifier Parser::parse_cv_qualifiers() {
	CVQualifier cv = CVQualifier::None;
	
	while (true) {
		if (peek() == "const"_tok) {
			cv = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv) | static_cast<uint8_t>(CVQualifier::Const));
			advance();
		} else if (peek() == "volatile"_tok) {
			cv = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv) | static_cast<uint8_t>(CVQualifier::Volatile));
			advance();
		} else {
			break;
		}
	}
	
	return cv;
}

// Helper function to parse reference qualifiers (& or &&) from the token stream
// Consolidates the repeated pattern of parsing reference operators throughout Parser.cpp
// Returns ReferenceQualifier: None, LValueReference, or RValueReference
ReferenceQualifier Parser::parse_reference_qualifier() {
	if (peek() == "&&"_tok) {
		advance();
		return ReferenceQualifier::RValueReference;
	} else if (peek() == "&"_tok) {
		advance();
		return ReferenceQualifier::LValueReference;
	}
	return ReferenceQualifier::None;
}

// Helper function to append template type argument suffix to a StringBuilder
// Consolidates the logic for building instantiated template names (e.g., "is_arithmetic_int")
// Previously duplicated in 4 locations throughout Parser.cpp
void Parser::append_type_name_suffix(StringBuilder& sb, const TemplateTypeArg& arg) {
	if (arg.is_value) {
		sb.append(static_cast<uint64_t>(arg.value));
	} else if (arg.base_type == Type::Void) {
		sb.append("void");
	} else if (arg.type_index > 0 && arg.type_index < gTypeInfo.size()) {
		sb.append(StringTable::getStringView(gTypeInfo[arg.type_index].name()));
	} else {
		sb.append(getTypeName(arg.base_type));
	}
}

ParseResult Parser::parse_type_specifier()
{
	// Add parsing depth check to prevent infinite loops
	if (++parsing_depth_ > MAX_PARSING_DEPTH) {
		--parsing_depth_;
		FLASH_LOG(Parser, Error, "Maximum parsing depth (", MAX_PARSING_DEPTH, ") exceeded in parse_type_specifier()");
		FLASH_LOG(Parser, Error, "Current token: ", current_token_.value());
		return ParseResult::error("Maximum parsing depth exceeded - possible infinite loop", current_token_);
	}
	
	// RAII guard to decrement depth on all exit paths
	struct DepthGuard {
		size_t& depth;
		~DepthGuard() { --depth; }
	} depth_guard{parsing_depth_};
	
	FLASH_LOG(Parser, Debug, "parse_type_specifier: Starting, current token: ", std::string(peek_info().value()));
	
	// Check for decltype or __typeof__/__typeof FIRST, before any other checks
	// __typeof__ and __typeof are GCC extensions that work like decltype
	// Note: decltype is a keyword (_tok), but __typeof__/__typeof are identifiers (string compare)
	if (peek() == "decltype"_tok || 
	    (!peek().is_eof() && (peek_info().value() == "__typeof__" || peek_info().value() == "__typeof"))) {
		return parse_decltype_specifier();
	}

	// Skip C++11 attributes that might appear before the type
	// e.g., [[nodiscard]] int foo();
	skip_cpp_attributes();

	// Skip function specifiers that might appear before the return type
	// e.g., constexpr int foo(), inline int bar(), static int baz()
	// These are not part of the type itself but function properties
	// Also skip noexcept which might appear in some parsing contexts
	while (!peek().is_eof()) {
		auto k = peek();
		if (k == "constexpr"_tok || k == "consteval"_tok || k == "constinit"_tok ||
		    k == "inline"_tok || k == "static"_tok || k == "extern"_tok ||
		    k == "virtual"_tok || k == "explicit"_tok || k == "friend"_tok) {
			advance(); // skip the function specifier
			// C++20 explicit(condition) - skip the condition expression
			if (k == "explicit"_tok && peek() == "("_tok) {
				skip_balanced_parens();
			}
			skip_cpp_attributes(); // there might be attributes after the specifier
		} else if (k == "noexcept"_tok) {
			skip_noexcept_specifier();
		} else {
			break;
		}
	}

	// Check for decltype or __typeof__/__typeof after function specifiers (e.g., static decltype(...))
	// This check MUST come after skipping function specifiers to handle patterns like:
	// "static decltype(_S_test_2<_Tp, _Up>(0))" which appear in standard library headers
	// __typeof__ and __typeof are GCC extensions that work like decltype
	if (peek() == "decltype"_tok || 
	    (!peek().is_eof() && (peek_info().value() == "__typeof__" || peek_info().value() == "__typeof"))) {
		return parse_decltype_specifier();
	}

	// Check for __underlying_type(T) which returns the underlying type of an enum
	// This is a type-returning intrinsic used in <type_traits>:
	//   using type = __underlying_type(_Tp);
	// Note: __underlying_type is an identifier, not a keyword — string compare is required
	if (!peek().is_eof() && peek_info().type() == Token::Type::Identifier &&
	    peek_info().value() == "__underlying_type") {
		Token underlying_token = peek_info();
		advance(); // consume '__underlying_type'

		// Expect '('
		if (peek() != "("_tok) {
			return ParseResult::error("Expected '(' after __underlying_type", underlying_token);
		}
		advance(); // consume '('

		// Parse the type argument
		ParseResult type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		// Expect ')'
		if (peek() != ")"_tok) {
			return ParseResult::error("Expected ')' after type in __underlying_type", current_token_);
		}
		advance(); // consume ')'

		// Get the argument type
		const TypeSpecifierNode& arg_type = type_result.node()->as<TypeSpecifierNode>();

		// If the argument is a template parameter or dependent type, create a dependent type placeholder
		if (arg_type.type() == Type::UserDefined && arg_type.type_index() == 0) {
			// Dependent type - return a placeholder that will be resolved during template instantiation
			FLASH_LOG(Templates, Debug, "parse_type_specifier: __underlying_type of dependent type, returning dependent placeholder");
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::UserDefined, TypeQualifier::None, 0, underlying_token, CVQualifier::None));
		}

		// Check if the argument is marked as a template parameter in current context
		if (parsing_template_body_ || !current_template_param_names_.empty()) {
			// Check if arg_type refers to a template parameter
			std::string_view arg_type_name = arg_type.token().value();
			for (const auto& param_name : current_template_param_names_) {
				if (arg_type_name == param_name) {
					FLASH_LOG(Templates, Debug, "parse_type_specifier: __underlying_type of template parameter '", arg_type_name, "', returning dependent placeholder");
					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						Type::UserDefined, TypeQualifier::None, 0, underlying_token, CVQualifier::None));
				}
			}
		}

		// For concrete enum types, resolve to the underlying type
		if (arg_type.type() == Type::Enum) {
			// Look up the enum type to get its underlying type
			if (arg_type.type_index() < gTypeInfo.size()) {
				const TypeInfo& enum_type_info = gTypeInfo[arg_type.type_index()];
				if (enum_type_info.enum_info_) {
					const EnumTypeInfo* enum_info = enum_type_info.enum_info_.get();
					Type underlying = enum_info->underlying_type;
					int underlying_size = enum_info->underlying_size;
					FLASH_LOG(Parser, Debug, "parse_type_specifier: __underlying_type resolved to ", static_cast<int>(underlying));
					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						underlying, TypeQualifier::None, underlying_size, underlying_token, CVQualifier::None));
				}
			}
		}

		// If we have a type index, try to look up if it's an enum
		if (arg_type.type_index() < gTypeInfo.size()) {
			const TypeInfo& type_info = gTypeInfo[arg_type.type_index()];
			if (type_info.enum_info_) {
				const EnumTypeInfo* enum_info = type_info.enum_info_.get();
				Type underlying = enum_info->underlying_type;
				int underlying_size = enum_info->underlying_size;
				FLASH_LOG(Parser, Debug, "parse_type_specifier: __underlying_type resolved to ", static_cast<int>(underlying));
				return ParseResult::success(emplace_node<TypeSpecifierNode>(
					underlying, TypeQualifier::None, underlying_size, underlying_token, CVQualifier::None));
			}
		}

		// For non-enum types in template context, return a placeholder
		// The actual type will be resolved when the template is instantiated
		FLASH_LOG(Templates, Debug, "parse_type_specifier: __underlying_type of non-enum or deferred, returning int placeholder");
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			Type::Int, TypeQualifier::None, 32, underlying_token, CVQualifier::None));
	}

	// Check for typename keyword (used in template-dependent contexts)
	// e.g., typename Container<T>::value_type
	// e.g., constexpr typename my_or<...>::type func()
	// This check MUST come after skipping function specifiers to handle patterns like:
	// "constexpr typename" which appear in standard library headers
	if (peek() == "typename"_tok) {
		advance(); // consume 'typename'
		// Continue parsing the actual type after typename
	}

	if (peek().is_eof() ||
		(!peek().is_keyword() && !peek().is_identifier() && peek() != "::"_tok)) {
		return ParseResult::error("Expected type specifier",
			peek().is_eof() ? Token() : peek_info());
	}

	size_t long_count = 0;
	TypeQualifier qualifier = TypeQualifier::None;
	CVQualifier cv_qualifier = CVQualifier::None;

	// Parse CV-qualifiers and type qualifiers in any order
	// e.g., "const int", "int const", "const unsigned int", "unsigned const int"
	bool parsing_qualifiers = true;
	while (parsing_qualifiers && !peek().is_eof()) {
		auto k = peek();
		if (k == "const"_tok) {
			cv_qualifier = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Const));
			advance();
		}
		else if (k == "volatile"_tok) {
			cv_qualifier = static_cast<CVQualifier>(
				static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Volatile));
			advance();
		}
		else if (k == "long"_tok) {
			long_count++;
			advance();
		}
		else if (k == "signed"_tok) {
			qualifier = TypeQualifier::Signed;
			advance();
		}
		else if (k == "unsigned"_tok) {
			qualifier = TypeQualifier::Unsigned;
			advance();
		}
		// C99/C11 complex type specifiers - consume and ignore for now
		// _Complex float, _Complex double, __complex__ double, etc.
		// FlashCpp doesn't yet support complex arithmetic, so we treat it as the base type
		// Note: _Complex/_Imaginary/__complex__ are identifiers, not keywords — string compare required
		else if (k.is_identifier()) {
			std::string_view ident = peek_info().value();
			if (ident == "_Complex" || ident == "__complex__" || ident == "_Imaginary") {
				advance();
			} else {
				parsing_qualifiers = false;
			}
		}
		// Microsoft-specific type modifiers - consume and ignore
		// __ptr32/__ptr64: Pointer size modifiers (32-bit vs 64-bit) - not needed for x64-only target
		// __sptr/__uptr: Signed/unsigned pointer extension - only relevant for 32/64-bit mixing
		// __w64: Deprecated 64-bit portability warning marker
		// __unaligned: Alignment hint - doesn't affect type parsing
		else if (k == "__ptr32"_tok || k == "__ptr64"_tok ||
		         k == "__w64"_tok || k == "__unaligned"_tok ||
		         k == "__uptr"_tok || k == "__sptr"_tok) {
			advance();
		}
		else {
			parsing_qualifiers = false;
		}
	}

	// Check for typename keyword AFTER cv-qualifiers
	// This handles patterns like: constexpr const typename tuple_element<...>::type
	// where "const" comes before "typename"
	if (peek() == "typename"_tok) {
		advance(); // consume 'typename'
		// Continue parsing the actual type after typename
	}

	// Static type map for most types. "long" and "wchar_t" are handled specially below.
	static const std::unordered_map<std::string_view, std::tuple<Type, size_t>>
		type_map = {
				{"void", {Type::Void, 0}},
				{"bool", {Type::Bool, 8}},
				{"char", {Type::Char, 8}},
				// Note: "wchar_t" is handled specially due to target-dependent size (16 on Windows, 32 on Linux)
				{"char8_t", {Type::Char8, 8}},         // C++20 UTF-8 character type
				{"char16_t", {Type::Char16, 16}},      // C++11 UTF-16 character type
				{"char32_t", {Type::Char32, 32}},      // C++11 UTF-32 character type
				{"short", {Type::Short, 16}},
				{"int", {Type::Int, 32}},
				// Note: "long" is handled specially due to target-dependent size
				{"float", {Type::Float, 32}},
				{"double", {Type::Double, 64}},
				{"auto", {Type::Auto, 0}},
				// Microsoft-specific type keywords
				{"__int8", {Type::Char, 8}},
				{"__int16", {Type::Short, 16}},
				{"__int32", {Type::Int, 32}},
				{"__int64", {Type::LongLong, 64}},
	};

	Type type = Type::UserDefined;
	int type_size = 0;

	// Check if we have a type keyword, or if we only have qualifiers (e.g., "long", "unsigned")
	bool has_explicit_type = false;
	if (!peek().is_eof()) {
		auto k = peek();
		// Handle "long" specially due to target-dependent size
		if (k == "long"_tok) {
			type = Type::Long;
			type_size = get_type_size_bits(Type::Long);
			has_explicit_type = true;
		// Handle "wchar_t" specially due to target-dependent size (16 on Windows, 32 on Linux)
		} else if (k == "wchar_t"_tok) {
			type = Type::WChar;
			type_size = get_wchar_size_bits();
			has_explicit_type = true;
		} else {
			// For type_map lookup we still need the spelling string
			const auto& it = type_map.find(peek_info().value());
			if (it != type_map.end()) {
				type = std::get<0>(it->second);
				type_size = static_cast<unsigned char>(std::get<1>(it->second));
				has_explicit_type = true;
			}
		}
	}

	// If we have an explicit type keyword, process it
	if (has_explicit_type) {

		// Apply signed/unsigned qualifier to integer types
		if (qualifier == TypeQualifier::Unsigned) {
			switch (type) {
				case Type::Char:
					type = Type::UnsignedChar;
					type_size = 8;
					break;
				case Type::Short:
					type = Type::UnsignedShort;
					type_size = 16;
					break;
				case Type::Int:
					type = Type::UnsignedInt;
					type_size = 32;
					break;
				case Type::Long:
					type = Type::UnsignedLong;
					type_size = get_type_size_bits(Type::UnsignedLong);
					break;
				default:
					break;
			}
		} else if (qualifier == TypeQualifier::Signed) {
			// Explicitly signed types keep their current type but ensure correct size
			switch (type) {
				case Type::Char:
					type_size = 8;
					break;
				case Type::Short:
					type_size = 16;
					break;
				case Type::Int:
					type_size = 32;
					break;
				case Type::Long:
					type_size = get_type_size_bits(Type::Long);
					break;
				default:
					break;
			}
		}

		if (long_count == 1) {
			if (type == Type::Float) {
				type_size = sizeof(long double);
			}
			else if (type == Type::Int) {
				// "long int" -> long
				type = Type::Long;
				type_size = get_type_size_bits(Type::Long);
			}
			else if (type == Type::UnsignedInt) {
				// "long unsigned int" or "unsigned long int" -> unsigned long
				type = Type::UnsignedLong;
				type_size = get_type_size_bits(Type::UnsignedLong);
			}
			else if (type == Type::Long) {
				// "long long" -> long long
				type = Type::LongLong;
				type_size = 64;
			}
			else if (type == Type::UnsignedLong) {
				// "long long unsigned" or "unsigned long long" -> unsigned long long
				type = Type::UnsignedLongLong;
				type_size = 64;
			}
		}
		else if (long_count == 2) {
			if (type == Type::Int) {
				// "long long int" -> long long
				type = Type::LongLong;
				type_size = 64;
			}
			else if (type == Type::UnsignedInt) {
				// "unsigned long long int" or "long long unsigned int" -> unsigned long long
				type = Type::UnsignedLongLong;
				type_size = 64;
			}
		}

		Token type_keyword_token = peek_info();
		advance();

		// Handle optional 'int' keyword after 'short', 'long', 'signed', or 'unsigned'
		// e.g., "short int", "long int", "unsigned int"
		if (peek() == "int"_tok &&
		    (type == Type::Short || type == Type::UnsignedShort ||
		     type == Type::Long || type == Type::UnsignedLong ||
		     type == Type::LongLong || type == Type::UnsignedLongLong)) {
			advance(); // consume optional 'int'
		}

		// Check for trailing CV-qualifiers (e.g., "int const", "float volatile")
		while (true) {
			if (peek() == "const"_tok) {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Const));
				advance();
			}
			else if (peek() == "volatile"_tok) {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Volatile));
				advance();
			}
			else {
				break;
			}
		}

		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, type_keyword_token, cv_qualifier));
	}
	else if (qualifier != TypeQualifier::None || long_count > 0) {
		// Handle cases like "unsigned", "signed", "long" without explicit type (defaults to int)
		// But NOT "const" alone - that could be "const Widget" which needs to continue
		// Examples: "unsigned" -> unsigned int, "signed" -> signed int, "long" -> long int

		if (long_count == 1) {
			// "long" or "const long" -> long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLong : Type::Long;
			type_size = get_type_size_bits(type);
		} else if (long_count == 2) {
			// "long long" or "const long long" -> long long int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedLongLong : Type::LongLong;
			type_size = 64;
		} else {
			// "unsigned", "signed" without type -> int
			type = (qualifier == TypeQualifier::Unsigned) ? Type::UnsignedInt : Type::Int;
			type_size = 32;
		}

		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			type, qualifier, type_size, Token(), cv_qualifier));
	}
	// If we only have CV-qualifiers (const/volatile) without unsigned/signed/long,
	// continue parsing - could be "const Widget&", "const int*", etc.
	// The following checks handle struct/class keywords and identifiers
	// Note: We don't handle 'enum' keyword here for type specifiers because:
	// - "enum TypeName" is handled in the identifier section below
	// - "enum : type { }" and "enum TypeName { }" are declarations, not type specifiers
	//   and should be caught by higher-level parsing (e.g., in struct member loop)
	else if (peek() == "struct"_tok || peek() == "class"_tok || peek() == "union"_tok) {
		// Handle "struct TypeName", "class TypeName", "union TypeName", and qualified names like "class std::type_info"
		advance(); // consume 'struct', 'class', or 'union'

		// Get the type name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected type name after 'struct', 'class', or 'union'",
			                          peek().is_eof() ? Token() : peek_info());
		}

		// Build qualified name using StringBuilder for efficiency
		StringBuilder type_name_builder;
		type_name_builder.append(peek_info().value());
		Token type_name_token = peek_info();
		advance();

		// Handle qualified names (e.g., std::type_info)
		while (peek() == "::"_tok) {
			advance();  // consume '::'
			// Handle ::template for dependent member templates
			// Example: typename _Tp::template rebind<_Up>
			if (peek() == "template"_tok) {
				advance();  // consume 'template'
			}
			
			if (!peek().is_identifier()) {
				type_name_builder.reset();  // Discard the builder
				return ParseResult::error("Expected identifier after '::'",
				                          peek().is_eof() ? Token() : peek_info());
			}
			type_name_builder.append("::"sv);
			type_name_builder.append(peek_info().value());
			type_name_token = peek_info();  // Update token for error reporting
			advance();  // consume identifier
		}

		StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name_builder.commit());

		// Look up the struct type
		auto type_it = gTypesByName.find(type_name_handle);
		if (type_it != gTypesByName.end() && type_it->second->isStruct()) {
			const TypeInfo* struct_type_info = type_it->second;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			// If this is a typedef to a struct (no struct_info but has type_index pointing to the actual struct),
			// follow the type_index to get the actual struct TypeInfo
			if (!struct_info && struct_type_info->type_index_ < gTypeInfo.size()) {
				const TypeInfo& actual_struct = gTypeInfo[struct_type_info->type_index_];
				if (actual_struct.isStruct() && actual_struct.getStructInfo()) {
					struct_type_info = &actual_struct;
					struct_info = actual_struct.getStructInfo();
				}
			}

			if (struct_info) {
				type_size = static_cast<int>(struct_info->total_size * 8);  // Convert bytes to bits
			} else {
				// Struct is being defined but not yet finalized (e.g., in member function parameters)
				// Use a placeholder size of 0 - it will be updated when the struct is finalized
				type_size = 0;
			}
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
		}

		// Forward declaration: struct not yet defined
		// Create a placeholder type entry for it
		// This allows pointers to undefined structs (e.g., struct Foo* ptr;)
		TypeInfo& forward_decl_type = add_struct_type(type_name_handle);
		type_size = 0;  // Unknown size until defined
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			Type::Struct, forward_decl_type.type_index_, type_size, type_name_token, cv_qualifier));
	}
	// Handle __builtin_va_list and __gnuc_va_list as GCC builtin types
	// These are used in <cstdarg> and libstdc++ headers for variadic argument handling.
	// They are registered as user-defined types in initialize_native_types().
	// Note: __builtin_va_list/__gnuc_va_list are identifiers, not keywords — string compare required
	else if (peek().is_identifier() &&
	         (peek_info().value() == "__builtin_va_list" || peek_info().value() == "__gnuc_va_list")) {
		Token va_list_token = peek_info();
		advance();
		auto type_name_handle = va_list_token.handle();
		auto type_it = gTypesByName.find(type_name_handle);
		if (type_it != gTypesByName.end()) {
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::UserDefined, TypeQualifier::None,
				static_cast<int>(type_it->second->type_size_),
				va_list_token, cv_qualifier));
		}
		// Fallback: treat as void*
		TypeSpecifierNode va_list_type(Type::Void, TypeQualifier::None, 0, va_list_token, cv_qualifier);
		va_list_type.add_pointer_level();
		return ParseResult::success(emplace_node<TypeSpecifierNode>(va_list_type));
	}
	else if (peek() == "::"_tok) {
		// Handle global-scope-qualified types like ::__gnu_debug::_Safe_iterator
		// The '::' prefix denotes a type from the global namespace
		advance();  // consume '::'

		if (!peek().is_identifier()) {
			return ParseResult::error("Expected identifier after '::'", peek().is_eof() ? Token() : peek_info());
		}

		// Build qualified name WITHOUT the leading :: prefix.
		// The :: is just a scope resolution indicator meaning "start from the global namespace"
		// and should not be part of the stored type name.
		StringBuilder type_name_builder;
		type_name_builder.append(peek_info().value());
		Token type_name_token = peek_info();
		advance();

		// Continue building qualified name (e.g., __gnu_debug::_Safe_iterator)
		while (peek() == "::"_tok) {
			advance();  // consume '::'
			if (peek() == "template"_tok) {
				advance();  // consume 'template'
			}
			if (!peek().is_identifier()) {
				type_name_builder.reset();
				return ParseResult::error("Expected identifier after '::'", peek().is_eof() ? Token() : peek_info());
			}
			type_name_builder.append("::"sv).append(peek_info().value());
			advance();
		}

		std::string_view type_name = type_name_builder.commit();

		// Update the token to carry the full qualified name (e.g., "__gnu_debug::_Safe_iterator")
		// so that downstream consumers like FriendDeclarationNode get the complete type name.
		type_name_token = Token(Token::Type::Identifier, type_name,
			type_name_token.line(), type_name_token.column(), type_name_token.file_index());

		// Skip template arguments if present (e.g., __gnu_debug::_Safe_iterator<_Ite, _Seq, Tag>)
		if (peek() == "<"_tok) {
			skip_template_arguments();
		}

		// Trailing CV-qualifiers
		while (peek() == "const"_tok || peek() == "volatile"_tok) {
			if (peek() == "const"_tok) {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Const));
			} else {
				cv_qualifier = static_cast<CVQualifier>(
					static_cast<uint8_t>(cv_qualifier) | static_cast<uint8_t>(CVQualifier::Volatile));
			}
			advance();
		}

		// Look up the type by its unqualified name
		StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name);
		auto type_it = gTypesByName.find(type_name_handle);
		if (type_it != gTypesByName.end()) {
			const TypeInfo* type_info = type_it->second;
			size_t user_type_index = type_info->type_index_;
			int type_size_bits = static_cast<int>(type_info->type_size_);

			// Determine the correct Type value from the found TypeInfo
			if (type_info->isStruct()) {
				const StructTypeInfo* struct_info = type_info->getStructInfo();
				if (struct_info) {
					type_size_bits = static_cast<int>(struct_info->total_size * 8);
				}
				return ParseResult::success(emplace_node<TypeSpecifierNode>(
					Type::Struct, user_type_index, type_size_bits, type_name_token, cv_qualifier));
			} else {
				// Use the type's own Type value (Enum, UserDefined, etc.)
				return ParseResult::success(emplace_node<TypeSpecifierNode>(
					type_info->type_, user_type_index, type_size_bits, type_name_token, cv_qualifier));
			}
		}

		// Not found - create a placeholder type (forward declaration)
		TypeInfo& forward_decl_type = add_struct_type(type_name_handle);
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			Type::Struct, forward_decl_type.type_index_, 0, type_name_token, cv_qualifier));
	}
	else if (peek().is_identifier()) {
		// Handle user-defined type (struct, class, or other user-defined types)
		// Build qualified name using StringBuilder for efficiency
		StringBuilder type_name_builder;
		type_name_builder.append(peek_info().value());
		Token type_name_token = peek_info();  // Save the token before consuming it
		advance();

		// Check for qualified name (e.g., Outer::Inner for nested classes)
		// Also track if ::template was used (indicates dependent member template access)
		bool has_explicit_template_keyword = false;
		while (peek() == "::"_tok) {
			advance();  // consume '::'

			// Handle ::template for dependent member templates
			// Example: typename _Tp::template rebind<_Up>
			// The 'template' keyword is a disambiguator for dependent contexts
			if (peek() == "template"_tok) {
				advance();  // consume 'template'
				has_explicit_template_keyword = true;  // Remember that ::template was used
			}
			
			if (!peek().is_identifier()) {
				type_name_builder.reset();  // Discard the builder
				return ParseResult::error("Expected identifier after '::'", peek().is_eof() ? Token() : peek_info());
			}

			type_name_builder.append("::"sv).append(peek_info().value());
			advance();
		}

		// Commit the StringBuilder to get a persistent string_view
		std::string_view type_name = type_name_builder.commit();

		// Check for template arguments: Container<int>
		std::optional<std::vector<TemplateTypeArg>> template_args;
		if (peek() == "<"_tok) {
			// Before parsing < as template arguments, check if the type name is actually a template
			// This prevents misinterpreting patterns like _R1::num < _R2::num> where < is comparison
			bool should_parse_as_template = true;
			
			// If ::template was used, always parse < as template arguments
			// This is the explicit template disambiguator for dependent contexts
			if (has_explicit_template_keyword) {
				should_parse_as_template = true;
			} else {
				// Check if this is a qualified name (contains ::)
				size_t last_colon_pos = type_name.rfind("::");
				if (last_colon_pos != std::string_view::npos) {
					// Extract the member name (part after the last ::)
					std::string_view member_name = type_name.substr(last_colon_pos + 2);
					
					// Check if the member is a known template
					auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
					auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
					
					// Also check with the full qualified name
					auto full_template_opt = gTemplateRegistry.lookupTemplate(type_name);
					auto full_var_template_opt = gTemplateRegistry.lookupVariableTemplate(type_name);
					
					bool member_is_template = member_template_opt.has_value() || 
					                          member_var_template_opt.has_value() ||
					                          full_template_opt.has_value() ||
					                          full_var_template_opt.has_value();
					
					if (!member_is_template) {
						// Member is NOT a known template
						// Check if the base (before ::) is a template parameter - if so, this is dependent
						// and we should NOT parse < as template arguments
						std::string_view base_name = type_name.substr(0, last_colon_pos);
						
						// Check if base is a template parameter
						bool base_is_template_param = false;
						for (const auto& param_name : current_template_param_names_) {
							if (StringTable::getStringView(param_name) == base_name) {
								base_is_template_param = true;
								break;
							}
						}
						
						if (base_is_template_param) {
							// Pattern like _R1::num where _R1 is a template parameter
							// The member 'num' is likely a static data member, not a template
							// Treat < as comparison operator
							FLASH_LOG_FORMAT(Templates, Debug, 
							    "Qualified name '{}' has template param base and non-template member - treating '<' as comparison operator",
							    type_name);
							should_parse_as_template = false;
						}
					}
				}
			}
			
			if (should_parse_as_template) {
				template_args = parse_explicit_template_arguments();
			}
			// If parsing succeeded, check if this is an alias template first
			if (template_args.has_value()) {
				// Check if this is an alias template
				FLASH_LOG_FORMAT(Parser, Debug, "Checking for alias template: '{}'", type_name);
				auto alias_opt = gTemplateRegistry.lookup_alias_template(type_name);
				if (alias_opt.has_value()) {
					FLASH_LOG_FORMAT(Parser, Debug, "Found alias template for '{}', is_deferred={}", type_name, alias_opt->as<TemplateAliasNode>().is_deferred());
					const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
					
					// Check for recursion: if we're already resolving this alias, return error
					if (resolving_aliases_.find(type_name) != resolving_aliases_.end()) {
						FLASH_LOG(Parser, Error, "Circular template alias dependency detected for '", type_name, "'");
						return ParseResult::error("Circular template alias dependency", type_name_token);
					}
					
					// Add this alias to the resolution stack
					resolving_aliases_.insert(type_name);
					
					// RAII guard to ensure we remove the alias from the stack even if we return early
					ScopeGuard alias_guard([this, type_name]() {
						resolving_aliases_.erase(type_name);
					});
					
					// OPTION 1: DEFERRED INSTANTIATION (preferred over string parsing)
					// Check if this alias uses deferred instantiation (target is a template with unresolved params)
					if (alias_node.is_deferred()) {
						FLASH_LOG(Parser, Debug, "Using deferred instantiation for alias '", type_name, "' -> '", alias_node.target_template_name(), "'");
						
						// Build substituted template arguments by replacing alias parameters with concrete values
						std::vector<TemplateTypeArg> substituted_args;
						const auto& param_names = alias_node.template_param_names();
						const auto& target_template_args = alias_node.target_template_args();
						
						// For each argument in the target template (e.g., bool, B in integral_constant<bool, B>)
						for (size_t i = 0; i < target_template_args.size(); ++i) {
							const ASTNode& arg_node = target_template_args[i];
							
							// Check if this argument is a type specifier
							if (arg_node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& arg_type = arg_node.as<TypeSpecifierNode>();
								
								// Check if this type is one of the alias template parameters
								// We check the token value, not type_index, because type_index may be 0 (placeholder) for template parameters
								bool is_alias_param = false;
								size_t alias_param_idx = 0;
								
								Token arg_token = arg_type.token();
								if (arg_token.type() == Token::Type::Identifier) {
									std::string_view arg_token_value = arg_token.value();
									for (size_t j = 0; j < param_names.size(); ++j) {
										if (arg_token_value == param_names[j].view()) {
											is_alias_param = true;
											alias_param_idx = j;
											break;
										}
									}
								}
								
								if (is_alias_param && alias_param_idx < template_args->size()) {
									// This argument references an alias parameter - substitute it with the actual argument
									// IMPORTANT: The alias argument might be a value (e.g., true), not a type
									FLASH_LOG(Parser, Debug, "Substituting alias parameter '", param_names[alias_param_idx].view(), "' at position ", alias_param_idx);
									substituted_args.push_back((*template_args)[alias_param_idx]);
								} else {
									// This argument is a concrete type - keep it as is
									substituted_args.push_back(TemplateTypeArg(arg_type));
								}
							}
						}
						
						FLASH_LOG(Parser, Debug, "Instantiating '", alias_node.target_template_name(), "' with ", substituted_args.size(), " substituted arguments");
						
						// Debug: log the substituted arguments
						for (size_t i = 0; i < substituted_args.size(); ++i) {
							const auto& arg = substituted_args[i];
							FLASH_LOG(Parser, Debug, "  Arg[", i, "]: is_value=", arg.is_value, 
							          ", base_type=", static_cast<int>(arg.base_type), 
							          ", value=", arg.value);
						}
						
						// Instantiate the target template with substituted arguments
						// First check if the target is itself a template alias (alias chaining)
						auto target_alias_opt = gTemplateRegistry.lookup_alias_template(alias_node.target_template_name());
						std::optional<ASTNode> instantiated_class;
						
						if (target_alias_opt.has_value()) {
							// Target is a template alias - resolve it recursively
							FLASH_LOG(Parser, Debug, "Target '", alias_node.target_template_name(), "' is a template alias - resolving recursively");
							
							const TemplateAliasNode& target_alias = target_alias_opt->as<TemplateAliasNode>();
							
							if (target_alias.is_deferred()) {
								// Recursively instantiate through the target alias
								// Build the substituted args for the target alias
								const auto& target_param_names = target_alias.template_param_names();
								const auto& target_target_args = target_alias.target_template_args();
								std::vector<TemplateTypeArg> nested_substituted_args;
								
								for (size_t i = 0; i < target_target_args.size(); ++i) {
									const ASTNode& arg_node = target_target_args[i];
									
									if (arg_node.is<TypeSpecifierNode>()) {
										const TypeSpecifierNode& arg_type = arg_node.as<TypeSpecifierNode>();
										
										// Check if this arg references a parameter of the target alias
										bool is_target_param = false;
										size_t target_param_idx = 0;
										
										Token arg_token = arg_type.token();
										if (arg_token.type() == Token::Type::Identifier) {
											std::string_view arg_token_value = arg_token.value();
											for (size_t j = 0; j < target_param_names.size(); ++j) {
												if (arg_token_value == target_param_names[j].view()) {
													is_target_param = true;
													target_param_idx = j;
													break;
												}
											}
										}
										
										if (is_target_param && target_param_idx < substituted_args.size()) {
											// Substitute with the argument we already substituted
											nested_substituted_args.push_back(substituted_args[target_param_idx]);
										} else {
											// Keep the concrete type
											nested_substituted_args.push_back(TemplateTypeArg(arg_type));
										}
									}
								}
								
								FLASH_LOG(Parser, Debug, "Nested instantiation: '", target_alias.target_template_name(), "' with ", nested_substituted_args.size(), " args");
								instantiated_class = try_instantiate_class_template(target_alias.target_template_name(), nested_substituted_args);
							} else {
								// Non-deferred target alias - fall back to regular class template instantiation
								instantiated_class = try_instantiate_class_template(alias_node.target_template_name(), substituted_args);
							}
						} else {
							// Target is a class template - instantiate directly
							instantiated_class = try_instantiate_class_template(alias_node.target_template_name(), substituted_args);
						}
						
						if (instantiated_class.has_value()) {
							// Add to AST if it's a struct
							if (instantiated_class->is<StructDeclarationNode>()) {
								ast_nodes_.push_back(*instantiated_class);
							}
							
							// Look up the instantiated type - need to determine the final template name
							// If we went through alias chaining, use the struct name from the instantiated class
							std::string_view instantiated_name;
							if (instantiated_class->is<StructDeclarationNode>()) {
								StringHandle name_handle = instantiated_class->as<StructDeclarationNode>().name();
								instantiated_name = StringTable::getStringView(name_handle);
							} else {
								instantiated_name = get_instantiated_class_name(alias_node.target_template_name(), substituted_args);
							}
							
							// Find the type by scanning gTypeInfo (safer than using gTypesByName pointer)
							TypeIndex type_idx = 0;
							bool found = false;
							StringHandle target_handle = StringTable::getOrInternStringHandle(instantiated_name);
							
							for (size_t i = 0; i < gTypeInfo.size(); ++i) {
								if (gTypeInfo[i].name() == target_handle) {
									type_idx = i;
									found = true;
									break;
								}
							}
							
							if (found) {
								const TypeInfo& new_ti = gTypeInfo[type_idx];
								
								FLASH_LOG(Parser, Debug, "Deferred instantiation succeeded: '", instantiated_name, "' at index ", type_idx);
								
								// Check for member type access after alias template resolution
								// Pattern: typename conditional_t<...>::type
								if (peek() == "::"_tok) {
									advance(); // consume '::'
									
									Token member_token = peek_info();
									if (member_token.type() == Token::Type::Identifier) {
										std::string_view member_name = member_token.value();
										advance(); // consume member name
										
										// Build qualified type name
										StringBuilder qualified_name_builder;
										std::string_view qualified_type_name = qualified_name_builder
											.append(instantiated_name)
											.append("::")
											.append(member_name)
											.commit();
										
										FLASH_LOG(Parser, Debug, "Looking up member type '", qualified_type_name, "' after alias resolution");
										
										// Look up the member type
										auto member_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_type_name));
										if (member_type_it != gTypesByName.end()) {
											const TypeInfo* member_type_info = member_type_it->second;
											FLASH_LOG(Parser, Debug, "Found member type '", qualified_type_name, "' at index ", member_type_info->type_index_);
											return ParseResult::success(emplace_node<TypeSpecifierNode>(
												member_type_info->type_, member_type_info->type_index_, 
												static_cast<unsigned char>(member_type_info->type_size_), 
												member_token, cv_qualifier));
										} else {
											// Member type not found - might be a dependent type
											FLASH_LOG(Parser, Debug, "Member type '", qualified_type_name, "' not found, creating placeholder");
											auto& placeholder_type = gTypeInfo.emplace_back();
											placeholder_type.type_ = Type::UserDefined;
											placeholder_type.type_index_ = gTypeInfo.size() - 1;
											placeholder_type.type_size_ = 0;
											placeholder_type.name_ = StringTable::getOrInternStringHandle(qualified_type_name);
											gTypesByName[placeholder_type.name_] = &placeholder_type;
											return ParseResult::success(emplace_node<TypeSpecifierNode>(
												Type::UserDefined, placeholder_type.type_index_, 0, member_token, cv_qualifier));
										}
									}
								}
								
								// Create the final type specifier
								auto new_type_spec = emplace_node<TypeSpecifierNode>(
									Type::Struct,
									type_idx,
									static_cast<unsigned char>(new_ti.type_size_),
									Token(),
									CVQualifier::None
								);
								
								return ParseResult::success(new_type_spec);
							} else {
							// Deferred instantiation didn't find the type, but this is often expected
							// for complex template metaprogramming patterns (SFINAE, etc.)
							// Fall through to simple alias handling
							FLASH_LOG(Parser, Debug, "Deferred instantiation: type '", instantiated_name, "' not found after instantiation at line ", type_name_token.line());
						}
					} else {
						// try_instantiate_class_template returned nullopt, which is expected for
						// dependent types and SFINAE patterns - fall through to simple alias handling
						FLASH_LOG(Parser, Debug, "Deferred instantiation failed for '", alias_node.target_template_name(), "' at line ", type_name_token.line());
						}
						
						// Fall through to simple alias handling if deferred instantiation failed or not applicable
					}
					
					// Handle non-deferred aliases (e.g., template<typename T> using Ptr = T*)
					// Substitute template arguments into the target type
					TypeSpecifierNode instantiated_type = alias_node.target_type_node();
					[[maybe_unused]] const auto& template_params = alias_node.template_parameters();
					const auto& param_names = alias_node.template_param_names();
					
					// Perform substitution for template parameters in the target type
					for (size_t i = 0; i < template_args->size() && i < param_names.size(); ++i) {
						const auto& arg = (*template_args)[i];
						std::string_view param_name = param_names[i].view();
						
						// Check if the target type refers to this template parameter
						// The target type will have Type::UserDefined and a type_index pointing to
						// the TypeInfo we created for the template parameter
						bool is_template_param = false;
						if (instantiated_type.type() == Type::UserDefined && instantiated_type.type_index() < gTypeInfo.size()) {
							const TypeInfo& ti = gTypeInfo[instantiated_type.type_index()];
							if (StringTable::getStringView(ti.name()) == param_name) {
								is_template_param = true;
							}
						}
						
						if (is_template_param) {
							// The target type is using this template parameter
							if (arg.is_value) {
								FLASH_LOG(Parser, Error, "Non-type template arguments not supported in alias templates yet");
								return ParseResult::error("Non-type template arguments not supported in alias templates", type_name_token);
							}
							
							// Save pointer/reference modifiers from target type
							size_t ptr_depth = instantiated_type.pointer_depth();
							bool is_ref = instantiated_type.is_reference();
							bool is_rval_ref = instantiated_type.is_rvalue_reference();
							CVQualifier cv = instantiated_type.cv_qualifier();
							
							// Get the size in bits for the argument type
							int size_bits = 0;
							if (arg.base_type == Type::Struct || arg.base_type == Type::UserDefined) {
								// Look up the struct size from type_index
								if (arg.type_index < gTypeInfo.size()) {
									const TypeInfo& ti = gTypeInfo[arg.type_index];
									size_bits = static_cast<unsigned char>(ti.type_size_);
								}
							} else {
								// Use standard type sizes
								size_bits = static_cast<unsigned char>(get_type_size_bits(arg.base_type));
							}
							
							// Create new type with substituted base type
							instantiated_type = TypeSpecifierNode(
								arg.base_type,
								arg.type_index,
								size_bits,
								Token(),  // No token for instantiated type
								cv
							);
							
							// Reapply pointer/reference modifiers from target type
							// e.g., if target is T* and we substitute int for T, we get int*
							for (size_t p = 0; p < ptr_depth; ++p) {
								instantiated_type.add_pointer_level(CVQualifier::None);
							}
							if (is_rval_ref) {
								instantiated_type.set_reference(true);  // rvalue ref
							} else if (is_ref) {
								instantiated_type.set_lvalue_reference(true);  // lvalue ref
							}
						}
					}
					
					// Check for member type access after alias template resolution
					// Pattern: typename alias_template<...>::type
					if (peek() == "::"_tok) {
						advance(); // consume '::'
						
						Token member_token = peek_info();
						if (member_token.type() == Token::Type::Identifier) {
							std::string_view member_name = member_token.value();
							advance(); // consume member name
							
							// Get the type name from instantiated_type to look up member
							std::string_view base_type_name;
							if (instantiated_type.type_index() < gTypeInfo.size()) {
								base_type_name = StringTable::getStringView(gTypeInfo[instantiated_type.type_index()].name());
							}
							
							// Build qualified type name
							StringBuilder qualified_name_builder;
							std::string_view qualified_type_name = qualified_name_builder
								.append(base_type_name)
								.append("::")
								.append(member_name)
								.commit();
							
							FLASH_LOG(Parser, Debug, "Looking up member type '", qualified_type_name, "' after non-deferred alias resolution");
							
							// Look up the member type
							auto member_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_type_name));
							if (member_type_it != gTypesByName.end()) {
								const TypeInfo* member_type_info = member_type_it->second;
								FLASH_LOG(Parser, Debug, "Found member type '", qualified_type_name, "' at index ", member_type_info->type_index_);
								return ParseResult::success(emplace_node<TypeSpecifierNode>(
									member_type_info->type_, member_type_info->type_index_, 
									static_cast<unsigned char>(member_type_info->type_size_), 
									member_token, cv_qualifier));
							} else {
								// Member type not found - might be a dependent type
								FLASH_LOG(Parser, Debug, "Member type '", qualified_type_name, "' not found, creating placeholder");
								auto& placeholder_type = gTypeInfo.emplace_back();
								placeholder_type.type_ = Type::UserDefined;
								placeholder_type.type_index_ = gTypeInfo.size() - 1;
								placeholder_type.type_size_ = 0;
								placeholder_type.name_ = StringTable::getOrInternStringHandle(qualified_type_name);
								gTypesByName[placeholder_type.name_] = &placeholder_type;
								return ParseResult::success(emplace_node<TypeSpecifierNode>(
									Type::UserDefined, placeholder_type.type_index_, 0, member_token, cv_qualifier));
							}
						}
					}
					
					return ParseResult::success(emplace_node<TypeSpecifierNode>(instantiated_type));
				}
				
				// Check if this is a template parameter being used with template arguments (e.g., Container<T>)
				// When parsing a template body, if the type name is a template parameter (type or template template param),
				// we should NOT try to instantiate it - it's a dependent type that will be resolved during instantiation
				bool is_dependent_template_param = false;
				if (parsing_template_body_ && !current_template_param_names_.empty()) {
					for (const auto& param_name : current_template_param_names_) {
						if (param_name == type_name) {
							is_dependent_template_param = true;
							break;
						}
					}
				}
				
				if (is_dependent_template_param) {
					// This is a template parameter being used with template arguments (e.g., Op<Args...>)
					// Check for nested type access (e.g., Op<Args...>::type) before returning early
					if (peek() == "::"_tok) {
						// Parse the nested type/member access
						advance(); // consume '::'
						
						// Handle optional 'template' keyword for dependent contexts
						if (peek() == "template"_tok) {
							advance(); // consume 'template'
						}
						
						// Get the nested identifier
						if (!peek().is_identifier()) {
							return ParseResult::error("Expected identifier after '::'", peek_info());
						}
						Token nested_token = peek_info();
						advance(); // consume the identifier
						
						// Build a dependent type name: Op<Args...>::type
						// For dependent types, we create a placeholder type that will be resolved during instantiation
						StringBuilder dependent_type_builder;
						dependent_type_builder.append(type_name);
						dependent_type_builder.append("<...>::");
						dependent_type_builder.append(nested_token.value());
						std::string_view dependent_type_name = dependent_type_builder.commit();
						
						// Create or look up a placeholder type for this dependent type
						auto type_handle = StringTable::getOrInternStringHandle(dependent_type_name);
						auto type_it = gTypesByName.find(type_handle);
						TypeIndex type_idx;
						if (type_it == gTypesByName.end()) {
							// Create a new placeholder type
							auto& placeholder_type = gTypeInfo.emplace_back();
							placeholder_type.type_ = Type::UserDefined;
							placeholder_type.type_index_ = gTypeInfo.size() - 1;
							placeholder_type.type_size_ = 0;
							placeholder_type.name_ = type_handle;
							gTypesByName[type_handle] = &placeholder_type;
							type_idx = placeholder_type.type_index_;
							FLASH_LOG(Templates, Debug, "Created placeholder for dependent nested type: ", dependent_type_name);
						} else {
							type_idx = type_it->second->type_index_;
						}
						
						auto type_spec_node = emplace_node<TypeSpecifierNode>(
							Type::UserDefined,
							type_idx,
							0,  // Size unknown for dependent type
							nested_token,
							cv_qualifier
						);
						return ParseResult::success(type_spec_node);
					}
					
					// No nested type access - create a dependent type reference
					// This will be resolved during instantiation of the containing template
					auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(type_name));
					if (type_it != gTypesByName.end()) {
						TypeIndex type_idx = type_it->second - &gTypeInfo[0];
						auto type_spec_node = emplace_node<TypeSpecifierNode>(
							Type::UserDefined,
							type_idx,
							0,  // Size unknown for dependent type
							type_name_token,
							CVQualifier::None
						);
						return ParseResult::success(type_spec_node);
					}
				}
				
				// Check if this is a variable template (like is_reference_v<T>) - if so, don't try to
				// instantiate as a class template. Variable templates are expressions, not types.
				// First try unqualified, then try namespace-qualified lookup
				auto var_template_check = gTemplateRegistry.lookupVariableTemplate(type_name);
				if (!var_template_check.has_value()) {
					NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
					if (!current_namespace.isGlobal()) {
						StringHandle type_handle = StringTable::getOrInternStringHandle(type_name);
						StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, type_handle);
						var_template_check = gTemplateRegistry.lookupVariableTemplate(StringTable::getStringView(qualified_handle));
					}
				}
				if (var_template_check.has_value()) {
					// This is a variable template, not a class template
					// In a type context, this is an error - return a failure so caller can handle
					FLASH_LOG_FORMAT(Templates, Debug, "Skipping class template instantiation for variable template '{}'", type_name);
					// Don't call try_instantiate_class_template - just continue to look up the type
					// The variable template instantiation should happen in expression context, not type context
				}
				
				std::optional<ASTNode> instantiated_class;
				if (!var_template_check.has_value()) {
					// Only try class template instantiation if this is NOT a variable template
					instantiated_class = try_instantiate_class_template(type_name, *template_args);
				}
				
				// If instantiation returned a struct node, add it to the AST so it gets visited during codegen
				if (instantiated_class.has_value() && instantiated_class->is<StructDeclarationNode>()) {
					ast_nodes_.push_back(*instantiated_class);
				}
				
				// Fill in default template arguments to get the actual instantiated name
				// (try_instantiate_class_template fills them internally, we need to do the same here)
				std::vector<TemplateTypeArg> filled_template_args = *template_args;
				auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
				if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					const auto& template_class = template_opt->as<TemplateClassDeclarationNode>();
					const auto& template_params = template_class.template_parameters();
					
					// Fill in defaults for missing parameters
					for (size_t i = filled_template_args.size(); i < template_params.size(); ++i) {
						if (!template_params[i].is<TemplateParameterNode>()) {
							FLASH_LOG_FORMAT(Templates, Error, "Template parameter {} is not a TemplateParameterNode", i);
							continue;
						}
						const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
						if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
							const ASTNode& default_node = param.default_value();
							if (default_node.is<TypeSpecifierNode>()) {
								const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
								filled_template_args.push_back(TemplateTypeArg(default_type));
							}
						} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
							// Handle non-type template parameter defaults like bool IsArith = is_arithmetic<T>::value
							const ASTNode& default_node = param.default_value();
							if (default_node.is<ExpressionNode>()) {
								const ExpressionNode& expr = default_node.as<ExpressionNode>();
								
								// Helper lambda to build instantiated template name suffix
								if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
									const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
									
									// Handle dependent static member access like is_arithmetic_void::value
									if (!qual_id.namespace_handle().isGlobal()) {
										std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id.namespace_handle());
										std::string_view member_name = qual_id.name();
										
										// Check if type_name is a dependent placeholder using TypeInfo-based detection
										auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
										if (is_dependent_placeholder && !filled_template_args.empty()) {
											
											// Build the instantiated template name using hash-based naming
											std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
											
											// Try to instantiate the template
											try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
											
											// Look up the instantiated type
											auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
											if (type_it != gTypesByName.end()) {
												const TypeInfo* type_info = type_it->second;
												if (type_info->getStructInfo()) {
													const StructTypeInfo* struct_info = type_info->getStructInfo();
													// Find the static member
													for (const auto& static_member : struct_info->static_members) {
														if (StringTable::getStringView(static_member.getName()) == member_name) {
															// Evaluate the static member's initializer
															if (static_member.initializer.has_value()) {
																const ASTNode& init_node = *static_member.initializer;
																if (init_node.is<ExpressionNode>()) {
																	const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																	if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																		bool val = std::get<BoolLiteralNode>(init_expr).value();
																		filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL));
																	} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																		const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																		const auto& val = lit.value();
																		if (std::holds_alternative<unsigned long long>(val)) {
																			filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																		}
																	}
																}
															}
															break;
														}
													}
												}
											}
										}
									}
								} else if (std::holds_alternative<NumericLiteralNode>(expr)) {
									const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
									const auto& val = lit.value();
									if (std::holds_alternative<unsigned long long>(val)) {
										filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
									} else if (std::holds_alternative<double>(val)) {
										filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
									}
								} else if (std::holds_alternative<BoolLiteralNode>(expr)) {
									const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
									filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL));
								}
							}
						}
					}
				}
				
				// Whether instantiation succeeded or returned nullopt (for specializations),
				// the type should now be registered. Look it up using filled args.
				std::string_view instantiated_name = get_instantiated_class_name(type_name, filled_template_args);
				
				// Check if any template arguments are dependent or pack expansions
				// If so, we cannot instantiate the template, but we can still parse ::member syntax
				// Check both filled_template_args (for is_dependent) and original template_args (for is_pack)
				bool has_dependent_args = false;
				for (const auto& arg : filled_template_args) {
					if (arg.is_dependent) {
						has_dependent_args = true;
						break;
					}
				}
				// Also check original template_args for pack expansions
				if (!has_dependent_args) {
					for (const auto& arg : *template_args) {
						if (arg.is_pack || arg.is_dependent) {
							has_dependent_args = true;
							break;
						}
					}
				}
				// Also check if the instantiated name itself contains dependent template arguments
				// (e.g., remove_cv<remove_reference<T>::type> where the arg is a dependent placeholder)
				if (!has_dependent_args) {
					auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
					if (type_it != gTypesByName.end()) {
						const TypeInfo* type_info = type_it->second;
						// Check if this instantiation has template arguments and any are dependent
						if (type_info->isTemplateInstantiation()) {
							const auto& template_arg_infos = type_info->templateArgs();
							for (const auto& arg_info : template_arg_infos) {
								// Check if argument is a UserDefined type (dependent placeholder)
								if (arg_info.base_type == Type::UserDefined && arg_info.type_index < gTypeInfo.size()) {
									has_dependent_args = true;
									FLASH_LOG_FORMAT(Templates, Debug, "Instantiated name '{}' has dependent template arguments", instantiated_name);
									break;
								}
							}
						}
					}
				}
				
				// Check for qualified name after template arguments: Template<T>::nested or Template<T>::type
				if (peek() == "::"_tok) {
					// Parse the qualified identifier path (e.g., Template<int>::Inner)
					bool had_template_keyword = false;
					auto qualified_result = parse_qualified_identifier_after_template(type_name_token, &had_template_keyword);
					if (qualified_result.is_error()) {
						FLASH_LOG(Parser, Error, "parse_qualified_identifier_after_template failed");
						return qualified_result;
					}
					
					// Build fully qualified type name using instantiated template name
					const auto& qualified_node = qualified_result.node()->as<QualifiedIdentifierNode>();
					// Get the qualified namespace name and append the identifier
					std::string_view ns_qualified = gNamespaceRegistry.getQualifiedName(qualified_node.namespace_handle());
					StringBuilder qualified_type_name_builder;
					qualified_type_name_builder.append(instantiated_name);
					// If there are additional namespace parts beyond the template, append them
					// The namespace handle might include parts beyond just the template name
					if (!ns_qualified.empty() && ns_qualified != type_name) {
						// Check if ns_qualified starts with type_name:: - if so, append the rest
						if (ns_qualified.starts_with(type_name) && ns_qualified.size() > type_name.size() + 2 &&
						    ns_qualified.substr(type_name.size(), 2) == "::") {
							qualified_type_name_builder.append(ns_qualified.substr(type_name.size()));
						}
					}
					qualified_type_name_builder.append("::").append(qualified_node.identifier_token().value());
					std::string_view qualified_type_name = qualified_type_name_builder.commit();
					
					// For dependent templates, if the qualified type is not found, check for template arguments
					// before creating a placeholder
					std::string_view member_name = qualified_node.identifier_token().value();
					bool has_template_args = (peek() == "<"_tok);
					
					if (has_dependent_args) {
						// Phase 4: Check for lazy nested type instantiation before lookup
						// If this is a nested type (e.g., outer_int::inner), try lazy instantiation
						StringHandle parent_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
						StringHandle nested_name_handle = StringTable::getOrInternStringHandle(member_name);
						if (LazyNestedTypeRegistry::getInstance().needsInstantiation(parent_name_handle, nested_name_handle)) {
							auto inst_result = instantiateLazyNestedType(parent_name_handle, nested_name_handle);
							if (inst_result.has_value()) {
								FLASH_LOG(Templates, Debug, "Used lazy nested type instantiation for: ", qualified_type_name);
							}
						}
						
						// Phase 3: Check for lazy type alias evaluation before lookup
						// If this is a member type alias (e.g., remove_const_int::type), try lazy evaluation
						StringHandle class_name_handle = StringTable::getOrInternStringHandle(instantiated_name);
						StringHandle member_name_handle_alias = StringTable::getOrInternStringHandle(member_name);
						if (LazyTypeAliasRegistry::getInstance().needsEvaluation(class_name_handle, member_name_handle_alias)) {
							auto eval_result = evaluateLazyTypeAlias(class_name_handle, member_name_handle_alias);
							if (eval_result.has_value()) {
								FLASH_LOG(Templates, Debug, "Used lazy type alias evaluation for: ", qualified_type_name);
							}
						}
						
						auto qual_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_type_name));
						if (qual_type_it == gTypesByName.end()) {
							// Type not found
							// If there are template arguments, we need to parse them and include in the type name
							// BUT: Check if the member is actually a known template first.
							// This avoids misinterpreting comparison operators like `R1::num < R2::num>`
							// as template arguments for `num`.
							// EXCEPTION: If the 'template' keyword was present (e.g., ::template type<Args>),
							// then we MUST treat the member as a template regardless of registry lookup.
							if (has_template_args && !had_template_keyword) {
								// Check if the member is a known template before parsing < as template arguments
								auto member_template_opt = gTemplateRegistry.lookupTemplate(member_name);
								auto member_var_template_opt = gTemplateRegistry.lookupVariableTemplate(member_name);
								
								// Also check with the full qualified name
								auto full_template_opt = gTemplateRegistry.lookupTemplate(qualified_type_name);
								auto full_var_template_opt = gTemplateRegistry.lookupVariableTemplate(qualified_type_name);
								
								bool member_is_template = member_template_opt.has_value() || 
								                          member_var_template_opt.has_value() ||
								                          full_template_opt.has_value() ||
								                          full_var_template_opt.has_value();
								
								if (!member_is_template) {
									// Member is NOT a known template, so < is likely a comparison operator
									// Don't parse it as template arguments - create placeholder without template args
									FLASH_LOG_FORMAT(Templates, Debug, 
									    "Member '{}' is not a known template - treating '<' as comparison operator, not template args",
									    member_name);
									has_template_args = false;  // Reset flag to skip template arg parsing
								}
							}
							
							if (has_template_args) {
								// Parse template arguments for the member (e.g., type<_If, _Else>)
								auto member_template_args = parse_explicit_template_arguments();
								if (!member_template_args.has_value()) {
									return ParseResult::error("Failed to parse template arguments for dependent member template", type_name_token);
								}
								
								// Append template arguments to qualified_type_name
								// For dependent types, include argument count for better debugging
								StringBuilder extended_name_builder;
								qualified_type_name = extended_name_builder
									.append(qualified_type_name)
									.append("<")
									.append(member_template_args->size())
									.append(" args>")
									.commit();
							}
							
							// Handle further nested type access after member template arguments
							// e.g., ::template rebind<_Tp>::other
							while (peek() == "::"_tok) {
								SaveHandle nested_pos = save_token_position();
								advance(); // consume '::'
								
								// Handle optional 'template' keyword
								if (peek() == "template"_tok) {
									advance(); // consume 'template'
								}
								
								if (peek().is_identifier()) {
									std::string_view nested_member = peek_info().value();
									advance(); // consume identifier
									
									StringBuilder nested_builder;
									qualified_type_name = nested_builder
										.append(qualified_type_name)
										.append("::")
										.append(nested_member)
										.commit();
									discard_saved_token(nested_pos);
									
									// Check for more template arguments on this nested member
									if (peek() == "<"_tok) {
										auto nested_tmpl_args = parse_explicit_template_arguments();
										if (nested_tmpl_args.has_value()) {
											StringBuilder tmpl_builder;
											qualified_type_name = tmpl_builder
												.append(qualified_type_name)
												.append("<")
												.append(nested_tmpl_args->size())
												.append(" args>")
												.commit();
										}
									}
								} else {
									restore_token_position(nested_pos);
									break;
								}
							}
							
							// Create a placeholder for the dependent qualified type
							FLASH_LOG_FORMAT(Templates, Debug, "Creating dependent type placeholder for {}", qualified_type_name);
							auto type_idx = StringTable::getOrInternStringHandle(qualified_type_name);
							auto& type_info = gTypeInfo.emplace_back();
							type_info.type_ = Type::UserDefined;
							type_info.type_index_ = gTypeInfo.size() - 1;
							type_info.type_size_ = 0;  // Unknown size for dependent type
							type_info.name_ = type_idx;
							gTypesByName[type_idx] = &type_info;
							
							return ParseResult::success(emplace_node<TypeSpecifierNode>(
								Type::UserDefined, type_info.type_index_, 0, type_name_token, cv_qualifier));
						}
						// If type IS found, continue with normal lookup below
					}
					
					// Look up the fully qualified type (e.g., "Traits_int::nested")
					auto qual_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(qualified_type_name));
					if (qual_type_it != gTypesByName.end()) {
						const TypeInfo* type_info = qual_type_it->second;
						
						// Handle both struct types and type aliases
						if (type_info->isStruct()) {
							const StructTypeInfo* struct_info = type_info->getStructInfo();

							if (struct_info) {
								type_size = static_cast<int>(struct_info->total_size * 8);
							} else {
								type_size = 0;
							}
							return ParseResult::success(emplace_node<TypeSpecifierNode>(
								Type::Struct, type_info->type_index_, type_size, type_name_token, cv_qualifier));
						} else {
							// This is a type alias - return the aliased type
							type_size = static_cast<unsigned char>(type_info->type_size_);
							return ParseResult::success(emplace_node<TypeSpecifierNode>(
								type_info->type_, type_info->type_index_, type_size, type_name_token, cv_qualifier));
						}
					}
					
					// Check if this might be a member template alias (e.g., Template<int>::type<Args>)
					// member_name and has_template_args already declared above
					
					// Check if the next token is '<', indicating template arguments for the member
					if (has_template_args) {
						// First try looking up with the instantiated name
						// Note: qualified_type_name already includes ::member_name from line 6689
						auto member_alias_opt = gTemplateRegistry.lookup_alias_template(qualified_type_name);
						
						// Keep a copy for error messages
						std::string member_alias_name_str = std::string(qualified_type_name);
						
						// If not found, check if this instantiation came from a partial specialization pattern
						if (!member_alias_opt.has_value()) {
							auto pattern_name_opt = gTemplateRegistry.get_instantiation_pattern(StringTable::getOrInternStringHandle(instantiated_name));
							if (pattern_name_opt.has_value()) {
								// This instantiation came from a partial specialization
								// Look up the member alias from the pattern
								StringBuilder pattern_builder;
								std::string_view pattern_member_alias_name = pattern_builder.append(*pattern_name_opt).append("::").append(member_name).preview();
								member_alias_opt = gTemplateRegistry.lookup_alias_template(pattern_member_alias_name);
								pattern_builder.reset();
							}
						}
						
						// If still not found, try with the base template name (for non-partial-spec cases)
						// Instantiated names have patterns like "ClassName_int" or "ClassName_int_1"
						// We need to find the original template name by progressively stripping suffixes
						if (!member_alias_opt.has_value()) {
							std::string_view base_template_name = instantiated_name;
							
							// Try progressively stripping '_suffix' patterns until we find a match
							while (!member_alias_opt.has_value() && !base_template_name.empty()) {
								size_t underscore_pos = base_template_name.find_last_of('_');
								if (underscore_pos == std::string_view::npos) {
									break;  // No more underscores to strip
								}
								
								base_template_name = base_template_name.substr(0, underscore_pos);
								if (base_template_name.empty()) {
									break;
								}
								
								StringBuilder base_builder;
								std::string_view base_member_alias_name = base_builder.append(base_template_name).append("::").append(member_name).preview();
								member_alias_opt = gTemplateRegistry.lookup_alias_template(base_member_alias_name);
								base_builder.reset();
							}
						}
						
						if (member_alias_opt.has_value()) {
							const TemplateAliasNode& alias_node = member_alias_opt->as<TemplateAliasNode>();
							
							// Parse template arguments for the member alias
							auto member_template_args = parse_explicit_template_arguments();
							if (!member_template_args.has_value()) {
								return ParseResult::error("Failed to parse template arguments for member template alias: " + member_alias_name_str, type_name_token);
							}
							
							// Instantiate the member template alias with the provided arguments
							TypeSpecifierNode instantiated_type = alias_node.target_type_node();
							[[maybe_unused]] const auto& template_params = alias_node.template_parameters();
							const auto& param_names = alias_node.template_param_names();

							// Perform substitution for template parameters in the target type
							for (size_t i = 0; i < member_template_args->size() && i < param_names.size(); ++i) {
								const auto& arg = (*member_template_args)[i];
								std::string_view param_name = param_names[i].view();
								
								// Check if the target type refers to this template parameter
								bool is_template_param = false;
								if (instantiated_type.type() == Type::UserDefined && instantiated_type.type_index() < gTypeInfo.size()) {
									const TypeInfo& ti = gTypeInfo[instantiated_type.type_index()];
									if (StringTable::getStringView(ti.name()) == param_name) {
										is_template_param = true;
									}
								}
								
								if (is_template_param) {
									// The target type is using this template parameter
									if (arg.is_value) {
										FLASH_LOG(Parser, Error, "Non-type template arguments not supported in member template aliases yet");
										return ParseResult::error("Non-type template arguments not supported in member template aliases", type_name_token);
									}
									
									// Save pointer/reference modifiers from target type
									size_t ptr_depth = instantiated_type.pointer_depth();
									bool is_ref = instantiated_type.is_reference();
									bool is_rval_ref = instantiated_type.is_rvalue_reference();
									CVQualifier cv_qual = instantiated_type.cv_qualifier();
									
									
									// Get the size in bits for the argument type
									int size_bits = 0;
									if (arg.base_type == Type::Struct || arg.base_type == Type::UserDefined) {
										// Look up the struct size from type_index
										if (arg.type_index < gTypeInfo.size()) {
											const TypeInfo& ti = gTypeInfo[arg.type_index];
											size_bits = static_cast<unsigned char>(ti.type_size_);
										}
									} else {
										// Use standard type sizes
										size_bits = static_cast<unsigned char>(get_type_size_bits(arg.base_type));
									}
									FLASH_LOG_FORMAT(Parser, Debug, "Before substitution - arg.base_type={}, size_bits={}", static_cast<int>(arg.base_type), size_bits);
									
									// Create new type with substituted base type
									instantiated_type = TypeSpecifierNode(
										arg.base_type,
										arg.type_index,
										size_bits,
										Token(),  // No token for instantiated type
										cv_qual
									);
									
									// Reapply pointer/reference modifiers from target type
									for (size_t p = 0; p < ptr_depth; ++p) {
										instantiated_type.add_pointer_level(CVQualifier::None);
									}
									if (is_rval_ref) {
										instantiated_type.set_reference(true);  // rvalue ref
									} else if (is_ref) {
										instantiated_type.set_lvalue_reference(true);  // lvalue ref
									}
								}
							}

							return ParseResult::success(emplace_node<TypeSpecifierNode>(instantiated_type));
						}
					}
					
					// If we're in a template body and the instantiated name contains "_unknown",
					// this is likely a template-dependent nested type that can't be resolved yet
					if (parsing_template_body_ && instantiated_name.find("_unknown") != std::string::npos) {
						// Create a placeholder UserDefined type for template-dependent nested types
						return ParseResult::success(emplace_node<TypeSpecifierNode>(
							Type::UserDefined, 0, 0, type_name_token, cv_qualifier));
					}
					
					// SFINAE: If we're in a substitution context and can't find the nested type,
					// this is a substitution failure, not a hard error
					if (in_sfinae_context_) {
						FLASH_LOG_FORMAT(Parser, Debug, "SFINAE: Substitution failure - unknown nested type: {}", qualified_type_name);
						// Return a placeholder type that will cause instantiation to fail
						// The caller (try_instantiate_single_template) will catch this and try the next overload
						StringBuilder error_builder;
						std::string_view error_msg = error_builder.append("SFINAE substitution failure: ").append(qualified_type_name).commit();
						return ParseResult::error(std::string(error_msg), type_name_token);
					}
					
					StringBuilder error_builder;
					std::string_view error_msg = error_builder.append("Unknown nested type: ").append(qualified_type_name).commit();
					return ParseResult::error(std::string(error_msg), type_name_token);
				}
				
				auto inst_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
				if (inst_type_it != gTypesByName.end()) {
					const TypeInfo* existing_type = inst_type_it->second;
					if (existing_type->isStruct()) {
						// Return existing struct type
						const StructTypeInfo* struct_info = existing_type->getStructInfo();
						if (struct_info) {
							type_size = static_cast<int>(struct_info->total_size * 8);
						} else {
							type_size = 0;
						}
						return ParseResult::success(emplace_node<TypeSpecifierNode>(
							Type::Struct, existing_type->type_index_, type_size, type_name_token, cv_qualifier));
					} else {
						// Return existing placeholder (UserDefined) - don't create duplicates
						return ParseResult::success(emplace_node<TypeSpecifierNode>(
							existing_type->type_, existing_type->type_index_, 0, type_name_token, cv_qualifier));
					}
				}

				// If type not found and we have dependent template args, create a placeholder type
				// with the full instantiated name (e.g., "is_function__Tp" instead of "is_function")
				// This preserves the dependent type information for use in nested template instantiations
				if (has_dependent_args) {
					FLASH_LOG_FORMAT(Templates, Debug, "Creating dependent template placeholder for '{}'", instantiated_name);
					auto type_idx = StringTable::getOrInternStringHandle(instantiated_name);
					auto& type_info = gTypeInfo.emplace_back();
					type_info.type_ = Type::UserDefined;
					type_info.type_index_ = gTypeInfo.size() - 1;
					type_info.type_size_ = 0;  // Unknown size for dependent type
					type_info.name_ = type_idx;
					gTypesByName[type_idx] = &type_info;
					
					// Set template instantiation metadata so isTemplateInstantiation() returns true
					// This is needed for deferred alias template detection
					auto template_args_info = convertToTemplateArgInfo(template_args.value());
					type_info.setTemplateInstantiationInfo(StringTable::getOrInternStringHandle(type_name), template_args_info);
					FLASH_LOG_FORMAT(Templates, Debug, "Set template instantiation metadata for dependent placeholder: base='{}', args={}",
					                 type_name, template_args_info.size());

					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						Type::UserDefined, type_info.type_index_, 0, type_name_token, cv_qualifier));
				}
				// If type not found, fall through to error handling below
			}
		}

		// Check if this is a template with all default parameters (e.g., Container instead of Container<>)
		auto template_opt = gTemplateRegistry.lookupTemplate(type_name);
		if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
			const auto& template_class = template_opt->as<TemplateClassDeclarationNode>();
			const auto& template_params = template_class.template_parameters();
			
			// Check if all parameters have defaults
			bool all_have_defaults = true;
			for (const auto& param_node : template_params) {
				if (param_node.is<TemplateParameterNode>()) {
					const auto& param = param_node.as<TemplateParameterNode>();
					if (!param.has_default()) {
						all_have_defaults = false;
						break;
					}
				}
			}
			
			if (all_have_defaults) {
				// Instantiate with empty args - defaults will be filled in
				std::vector<TemplateTypeArg> empty_args;
				auto instantiated_class = try_instantiate_class_template(type_name, empty_args);
				
				// Fill in default template arguments to get the actual instantiated name
				// Helper lambda to build instantiated template name suffix
				std::vector<TemplateTypeArg> filled_template_args;
				for (size_t i = 0; i < template_params.size(); ++i) {
					if (!template_params[i].is<TemplateParameterNode>()) {
						FLASH_LOG_FORMAT(Templates, Error, "Template parameter {} is not a TemplateParameterNode", i);
						continue;
					}
					const TemplateParameterNode& param = template_params[i].as<TemplateParameterNode>();
					if (param.has_default() && param.kind() == TemplateParameterKind::Type) {
						const ASTNode& default_node = param.default_value();
						if (default_node.is<TypeSpecifierNode>()) {
							const TypeSpecifierNode& default_type = default_node.as<TypeSpecifierNode>();
							filled_template_args.push_back(TemplateTypeArg(default_type));
						}
					} else if (param.has_default() && param.kind() == TemplateParameterKind::NonType) {
						const ASTNode& default_node = param.default_value();
						if (default_node.is<ExpressionNode>()) {
							const ExpressionNode& expr = default_node.as<ExpressionNode>();
							
							if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
								const QualifiedIdentifierNode& qual_id = std::get<QualifiedIdentifierNode>(expr);
								
							if (!qual_id.namespace_handle().isGlobal()) {
								std::string_view type_name_sv = gNamespaceRegistry.getName(qual_id.namespace_handle());
								std::string_view member_name = qual_id.name();
								
								// Check for dependent placeholder using TypeInfo-based detection
								auto [is_dependent_placeholder, template_base_name] = isDependentTemplatePlaceholder(type_name_sv);
								if (is_dependent_placeholder && !filled_template_args.empty()) {
									// Build the instantiated template name using hash-based naming
									std::string_view inst_name = get_instantiated_class_name(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
										
										try_instantiate_class_template(template_base_name, std::vector<TemplateTypeArg>{filled_template_args[0]});
										
										auto type_it_inner = gTypesByName.find(StringTable::getOrInternStringHandle(inst_name));
										if (type_it_inner != gTypesByName.end()) {
											const TypeInfo* type_info = type_it_inner->second;
											if (type_info->getStructInfo()) {
												const StructTypeInfo* struct_info_inner = type_info->getStructInfo();
												for (const auto& static_member : struct_info_inner->static_members) {
													if (StringTable::getStringView(static_member.getName()) == member_name) {
														if (static_member.initializer.has_value()) {
															const ASTNode& init_node = *static_member.initializer;
															if (init_node.is<ExpressionNode>()) {
																const ExpressionNode& init_expr = init_node.as<ExpressionNode>();
																if (std::holds_alternative<BoolLiteralNode>(init_expr)) {
																	bool val = std::get<BoolLiteralNode>(init_expr).value();
																	filled_template_args.push_back(TemplateTypeArg(val ? 1LL : 0LL));
																} else if (std::holds_alternative<NumericLiteralNode>(init_expr)) {
																	const NumericLiteralNode& lit = std::get<NumericLiteralNode>(init_expr);
																	const auto& val = lit.value();
																	if (std::holds_alternative<unsigned long long>(val)) {
																		filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
																	}
																}
															}
														}
														break;
													}
												}
											}
										}
									}
								}
							} else if (std::holds_alternative<NumericLiteralNode>(expr)) {
								const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
								const auto& val = lit.value();
								if (std::holds_alternative<unsigned long long>(val)) {
									filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<unsigned long long>(val))));
								} else if (std::holds_alternative<double>(val)) {
									filled_template_args.push_back(TemplateTypeArg(static_cast<int64_t>(std::get<double>(val))));
								}
							} else if (std::holds_alternative<BoolLiteralNode>(expr)) {
								const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
								filled_template_args.push_back(TemplateTypeArg(lit.value() ? 1LL : 0LL));
							}
						}
					}
				}
				
				std::string_view instantiated_name = get_instantiated_class_name(type_name, filled_template_args);
				
				auto inst_type_it = gTypesByName.find(StringTable::getOrInternStringHandle(instantiated_name));
				if (inst_type_it != gTypesByName.end() && inst_type_it->second->isStruct()) {
					const TypeInfo* struct_type_info = inst_type_it->second;
					const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

					if (struct_info) {
						type_size = static_cast<int>(struct_info->total_size * 8);
					} else {
						type_size = 0;
					}
					return ParseResult::success(emplace_node<TypeSpecifierNode>(
						Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier));
				}
			}
		}

		// Check if the identifier is a template parameter (e.g., _Tp in template<typename _Tp>)
		// This must be checked BEFORE looking up in gTypesByName to handle patterns like:
		// __has_trivial_destructor(_Tp) where _Tp is a template parameter
		// IMPORTANT: Skip this check during SFINAE context (in_sfinae_context_), because in that case
		// the template parameters have been substituted with concrete types in gTypesByName, and we
		// should use the substituted types instead of creating dependent type placeholders.
		if (parsing_template_body_ && !current_template_param_names_.empty() && !in_sfinae_context_) {
			StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name);
			for (const auto& param_name : current_template_param_names_) {
				if (param_name == type_name_handle) {
					// This is a template parameter - create a dependent type placeholder
					// Look up the TypeInfo for this parameter (it should have been registered when
					// the template parameters were parsed)
					auto param_type_it = gTypesByName.find(param_name);
					if (param_type_it != gTypesByName.end()) {
						TypeIndex param_type_idx = param_type_it->second->type_index_;
						FLASH_LOG_FORMAT(Templates, Debug, 
							"parse_type_specifier: '{}' is a template parameter, returning dependent type at index {}", 
							type_name, param_type_idx);
						return ParseResult::success(emplace_node<TypeSpecifierNode>(
							Type::UserDefined, param_type_idx, 0, type_name_token, cv_qualifier));
					} else {
						// Template parameter not yet in gTypesByName - create a placeholder
						// This can happen when parsing the template parameter list itself
						FLASH_LOG_FORMAT(Templates, Debug, 
							"parse_type_specifier: '{}' is a template parameter (not yet registered), creating placeholder", 
							type_name);
						auto& type_info = gTypeInfo.emplace_back();
						type_info.type_ = Type::UserDefined;
						type_info.type_index_ = gTypeInfo.size() - 1;
						type_info.type_size_ = 0;  // Unknown size for dependent type
						type_info.name_ = type_name_handle;
						gTypesByName[type_name_handle] = &type_info;
						return ParseResult::success(emplace_node<TypeSpecifierNode>(
							Type::UserDefined, type_info.type_index_, 0, type_name_token, cv_qualifier));
					}
				}
			}
		}

        // Check if this is a registered struct type (considering current namespace context)
        StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_name);
        const TypeInfo* type_info_ctx = lookupTypeInCurrentContext(type_name_handle);
        if (type_info_ctx && type_info_ctx->isStruct()) {
			// This is a struct type (or a typedef to a struct type)
			const TypeInfo* original_type_info = type_info_ctx;  // Keep reference to original for checking ref qualifiers
			const TypeInfo* struct_type_info = type_info_ctx;
			const StructTypeInfo* struct_info = struct_type_info->getStructInfo();

			// If this is a typedef to a struct (no struct_info but has type_index pointing to the actual struct),
			// follow the type_index to get the actual struct TypeInfo
			if (!struct_info && struct_type_info->type_index_ < gTypeInfo.size()) {
				const TypeInfo& actual_struct = gTypeInfo[struct_type_info->type_index_];
				if (actual_struct.isStruct() && actual_struct.getStructInfo()) {
					struct_type_info = &actual_struct;
					struct_info = actual_struct.getStructInfo();
				}
			}

			if (struct_info) {
				type_size = static_cast<int>(struct_info->total_size * 8);  // Convert bytes to bits
			} else {
				// Struct is being defined but not yet finalized (e.g., in member function parameters)
				// Use a placeholder size of 0 - it will be updated when the struct is finalized
				type_size = 0;
			}
			
			// Create the TypeSpecifierNode for the struct
			auto type_spec_node = emplace_node<TypeSpecifierNode>(
				Type::Struct, struct_type_info->type_index_, type_size, type_name_token, cv_qualifier);
			
			// If this is a type alias with reference qualifiers (e.g., using ReturnType = Value&&),
			// we need to preserve those reference qualifiers on the returned TypeSpecifierNode
			if (original_type_info->is_reference_) {
				if (original_type_info->is_rvalue_reference_) {
					type_spec_node.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
				} else {
					type_spec_node.as<TypeSpecifierNode>().set_lvalue_reference(true);  // lvalue reference
				}
			}

			// Also preserve pointer depth if the alias has pointers
			type_spec_node.as<TypeSpecifierNode>().add_pointer_levels(original_type_info->pointer_depth_);
			
			return ParseResult::success(type_spec_node);
		}

		// Check if this is a registered enum type
		if (type_info_ctx && type_info_ctx->isEnum()) {
			// This is an enum type
			const TypeInfo* enum_type_info = type_info_ctx;
			const EnumTypeInfo* enum_info = enum_type_info->getEnumInfo();

			if (enum_info) {
				type_size = enum_info->underlying_size;
			} else {
				// Enum is being defined but not yet finalized
				type_size = 32;  // Default to int size
			}
			return ParseResult::success(emplace_node<TypeSpecifierNode>(
				Type::Enum, enum_type_info->type_index_, type_size, type_name_token, cv_qualifier));
		}

		// Otherwise, treat as generic user-defined type or typedef
		// Look up the type_index if it's a registered type
		TypeIndex user_type_index = 0;
		Type resolved_type = Type::UserDefined;
		if (type_info_ctx) {
			user_type_index = type_info_ctx->type_index_;
			// If this is a typedef (has a stored type and size, but is not a struct/enum), use the underlying type
			bool is_typedef = (type_info_ctx->type_size_ > 0 && !type_info_ctx->isStruct() && !type_info_ctx->isEnum());
			// Also consider function pointer/reference type aliases as typedefs (they may have size 0 but have function_signature)
			if (!is_typedef && type_info_ctx->function_signature_.has_value()) {
				is_typedef = true;
			}
			// Also consider reference type aliases as typedefs (they may have size 0 but have reference qualifiers)
			// This is critical for std::move's ReturnType which is typename remove_reference<T>::type&&
			if (!is_typedef && type_info_ctx->is_reference_) {
				is_typedef = true;
			}
			if (is_typedef) {
				resolved_type = type_info_ctx->type_;
				type_size = type_info_ctx->type_size_;
				// Create TypeSpecifierNode and add pointer levels and reference qualifiers from typedef
				auto type_spec_node = emplace_node<TypeSpecifierNode>(
					resolved_type, user_type_index, type_size, type_name_token, cv_qualifier);
				type_spec_node.as<TypeSpecifierNode>().add_pointer_levels(type_info_ctx->pointer_depth_);
				// Add reference qualifiers from typedef
				if (type_info_ctx->is_reference_) {
					if (type_info_ctx->is_rvalue_reference_) {
						type_spec_node.as<TypeSpecifierNode>().set_reference(true);  // rvalue reference
					} else {
						type_spec_node.as<TypeSpecifierNode>().set_lvalue_reference(true);  // lvalue reference
					}
				}
				// Copy function signature for function pointer/reference type aliases
				if (type_info_ctx->function_signature_.has_value()) {
					type_spec_node.as<TypeSpecifierNode>().set_function_signature(type_info_ctx->function_signature_.value());
				}
				return ParseResult::success(type_spec_node);
			} else if (user_type_index < gTypeInfo.size()) {
				// Not a typedef - might be a struct type without size set in TypeInfo
				// Look up actual size from struct info if available
				const TypeInfo& actual_type_info = gTypeInfo[user_type_index];
				if (actual_type_info.isStruct()) {
					const StructTypeInfo* struct_info = actual_type_info.getStructInfo();
					if (struct_info) {
						type_size = static_cast<int>(struct_info->total_size * 8);
					}
				}
			}
		}
		return ParseResult::success(emplace_node<TypeSpecifierNode>(
			resolved_type, user_type_index, type_size, type_name_token, cv_qualifier));
	}

	std::string error_msg = "Unexpected token in type specifier";
	if (!peek().is_eof()) {
		error_msg += ": '";
		error_msg += peek_info().value();
		error_msg += "'";
	}
	return ParseResult::error(error_msg, peek().is_eof() ? Token() : peek_info());
}

ParseResult Parser::parse_decltype_specifier()
{
	// Parse decltype(expr) or decltype(auto) or __typeof__(expr) type specifier
	// Example: decltype(x + y) result = x + y;
	// Example: decltype(auto) result = x + y;  // C++14 deduced return type
	// __typeof__ is a GCC extension that works like decltype

	ScopedTokenPosition saved_position(*this);

	// Consume 'decltype' or '__typeof__' keyword
	Token decltype_token = advance();
	std::string_view keyword = decltype_token.value();

	// Expect '('
	if (!consume("("_tok)) {
		return ParseResult::error(std::string("Expected '(' after '") + std::string(keyword) + "'", current_token_);
	}

	// C++14: Check for decltype(auto) - special case for deduced return types
	// decltype(auto) deduces the type preserving references and cv-qualifiers
	if (keyword == "decltype" && peek() == "auto"_tok) {
		advance();  // consume 'auto'
		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after 'decltype(auto)'", current_token_);
		}
		// Return Type::Auto to indicate deduced return type
		// The semantics of decltype(auto) vs auto differ during instantiation,
		// but for parsing purposes, we treat it as auto with special handling
		TypeSpecifierNode auto_type(Type::Auto, TypeQualifier::None, 0);
		return saved_position.success(emplace_node<TypeSpecifierNode>(auto_type));
	}

	// Phase 3: Parse the expression with Decltype context for proper template disambiguation
	// In decltype context, < after qualified-id should strongly prefer template arguments over comparison
	SaveHandle expr_start_pos = save_token_position();
	ParseResult expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Decltype);
	if (expr_result.is_error()) {
		// If we're in a template context and the expression parsing fails (e.g., due to
		// unresolved function calls with dependent arguments), create a dependent type
		// placeholder instead of propagating the error. The actual function lookup
		// will happen during template instantiation when the dependent types are known.
		if (parsing_template_body_ || !current_template_param_names_.empty()) {
			FLASH_LOG(Templates, Debug, "Creating dependent type for failed decltype expression in template context");
			// Restore position to start of expression for reliable paren counting
			restore_token_position(expr_start_pos);
			// Skip to closing ')' to recover parsing - depth 1 for the opening decltype(
			int paren_depth = 1;
			while (!peek().is_eof() && paren_depth > 0) {
				if (peek() == "("_tok) {
					paren_depth++;
				} else if (peek() == ")"_tok) {
					paren_depth--;
					if (paren_depth == 0) break;  // Don't consume the final ')'
				}
				advance();
			}
			// Consume the final ')'
			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after decltype expression", current_token_);
			}
			// Create a placeholder type for the dependent decltype expression
			TypeSpecifierNode dependent_type(Type::Auto, TypeQualifier::None, 0);
			return saved_position.success(emplace_node<TypeSpecifierNode>(dependent_type));
		}
		discard_saved_token(expr_start_pos);
		return expr_result;
	}
	discard_saved_token(expr_start_pos);

	// Expect ')'
	if (!consume(")"_tok)) {
		return ParseResult::error("Expected ')' after decltype expression", current_token_);
	}

	// Deduce the type from the expression
	auto type_spec_opt = get_expression_type(*expr_result.node());
	if (!type_spec_opt.has_value()) {
		// If we're in a template body/declaration and the expression is dependent,
		// create a dependent type placeholder that will be resolved during instantiation.
		// Check both parsing_template_body_ and current_template_param_names_ since
		// some template contexts (like member function templates in structs) might not
		// set parsing_template_body_ but will have template parameter names.
		if (parsing_template_body_ || !current_template_param_names_.empty()) {
			FLASH_LOG(Templates, Debug, "Creating dependent type for decltype expression in template context");
			// Create a placeholder type for the dependent decltype expression
			// Store the expression so it can be re-evaluated during instantiation
			TypeSpecifierNode dependent_type(Type::Auto, TypeQualifier::None, 0);
			// Mark it as dependent/unresolved - it will be resolved during template instantiation
			return saved_position.success(emplace_node<TypeSpecifierNode>(dependent_type));
		}
		return ParseResult::error("Could not deduce type from decltype expression", decltype_token);
	}

	// Return the deduced type specifier
	return saved_position.success(emplace_node<TypeSpecifierNode>(*type_spec_opt));
}

// Phase 1: Unified parameter list parsing
// This method handles all the common parameter parsing logic:
// - Basic parameters: (int x, float y)
// - Variadic parameters: (int x, ...)
// - Default values: (int x = 0, float y = 1.0)
// - Empty parameter lists: ()
ParseResult Parser::parse_parameter_list(FlashCpp::ParsedParameterList& out_params, CallingConvention calling_convention)
{
	out_params.parameters.clear();
	out_params.is_variadic = false;

	if (!consume("("_tok)) {
		return ParseResult::error("Expected '(' for parameter list", current_token_);
	}

	while (!consume(")"_tok)) {
		// Handle C-style (void) parameter list meaning "no parameters"
		// In C/C++, f(void) is equivalent to f()
		if (out_params.parameters.empty() && peek() == "void"_tok) {
			// Check if this is exactly "(void)" - void followed by ')'
			SaveHandle void_check = save_token_position();
			advance(); // consume 'void'
			if (peek() == ")"_tok) {
				// This is (void) - empty parameter list
				discard_saved_token(void_check);
				advance(); // consume ')'
				break;
			}
			// Not (void), restore and continue with normal parameter parsing
			restore_token_position(void_check);
		}

		// Check for variadic parameter (...)
		if (peek() == "..."_tok) {
			advance(); // consume '...'
			out_params.is_variadic = true;

			// Validate calling convention for variadic functions
			// Only __cdecl and __vectorcall support variadic parameters (caller cleanup)
			if (calling_convention != CallingConvention::Default &&
			    calling_convention != CallingConvention::Cdecl &&
			    calling_convention != CallingConvention::Vectorcall) {
				return ParseResult::error(
					"Variadic functions must use __cdecl or __vectorcall calling convention "
					"(other conventions use callee cleanup which is incompatible with variadic arguments)",
					current_token_);
			}

			if (!consume(")"_tok)) {
				return ParseResult::error("Expected ')' after variadic '...'", current_token_);
			}
			break;
		}

		// Parse parameter type and name
		ParseResult type_and_name_result = parse_type_and_name();
		if (type_and_name_result.is_error()) {
			return type_and_name_result;
		}

		if (auto node = type_and_name_result.node()) {
			// Apply array-to-pointer decay for function parameters
			// In C++, function parameters declared as T arr[N] are treated as T* arr
			if (node->is<DeclarationNode>()) {
				auto& decl = node->as<DeclarationNode>();
				if (decl.array_size().has_value()) {
					// This is an array parameter - convert to pointer
					// Get the underlying type and add a pointer level
					const TypeSpecifierNode& orig_type = decl.type_node().as<TypeSpecifierNode>();
					TypeSpecifierNode param_type = orig_type;  // Copy needed since we modify
					param_type.add_pointer_level();
					
					// Create new declaration without array size (now a pointer)
					ASTNode new_decl = emplace_node<DeclarationNode>(
						emplace_node<TypeSpecifierNode>(param_type),
						decl.identifier_token()
					);
					
					// Copy over any other attributes
					if (decl.has_default_value()) {
						new_decl.as<DeclarationNode>().set_default_value(decl.default_value());
					}
					if (decl.is_parameter_pack()) {
						new_decl.as<DeclarationNode>().set_parameter_pack(true);
					}
					
					out_params.parameters.push_back(new_decl);
				} else {
					out_params.parameters.push_back(*node);
				}
			} else {
				out_params.parameters.push_back(*node);
			}
		}

		// Parse default parameter value (if present)
		// Note: '=' is an Operator token, not a Punctuator token
		if (peek() == "="_tok) {
			advance(); // consume '='
			// Parse the default value expression
			auto default_value = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (default_value.is_error()) {
				return default_value;
			}
			// Store default value in parameter node
			if (default_value.node().has_value() && !out_params.parameters.empty()) {
				auto& last_param = out_params.parameters.back();
				if (last_param.is<DeclarationNode>()) {
					last_param.as<DeclarationNode>().set_default_value(*default_value.node());
				}
			}
		}

		// Skip GCC attributes on parameters (e.g., __attribute__((__unused__)))
		skip_gcc_attributes();

		if (consume(","_tok)) {
			// After a comma, check if the next token is '...' for variadic parameters
			if (peek() == "..."_tok) {
				advance(); // consume '...'
				out_params.is_variadic = true;

				// Validate calling convention for variadic functions
				if (calling_convention != CallingConvention::Default &&
				    calling_convention != CallingConvention::Cdecl &&
				    calling_convention != CallingConvention::Vectorcall) {
					return ParseResult::error(
						"Variadic functions must use __cdecl or __vectorcall calling convention "
						"(other conventions use callee cleanup which is incompatible with variadic arguments)",
						current_token_);
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after variadic '...'", current_token_);
				}
				break;
			}
			continue;
		}
		else if (consume(")"_tok)) {
			break;
		}
		else {
			return ParseResult::error("Expected ',' or ')' in parameter list", current_token_);
		}
	}

	return ParseResult::success();
}

// Unified function call argument parsing
// This method consolidates the 6+ places where function call arguments are parsed in the codebase.
// It handles:
// - Comma-separated argument list parsing
// - Pack expansion (...) after arguments
// - Optional argument type collection for template deduction
// - Simple pack identifier expansion (for already-expanded packs in symbol table)
FlashCpp::ParsedFunctionArguments Parser::parse_function_arguments(const FlashCpp::FunctionArgumentContext& ctx)
{
	using namespace FlashCpp;
	
	// Check if function call has arguments (not empty parentheses)
	if (peek().is_eof() || peek() == ")"_tok) {
		// Empty argument list - return empty result without allocating
		ParsedFunctionArguments result;
		result.success = true;
		return result;
	}
	
	// We have arguments, so allocate storage
	ChunkedVector<ASTNode> args;
	std::vector<TypeSpecifierNode> arg_types;
	
	while (true) {
		auto arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (arg_result.is_error()) {
			return ParsedFunctionArguments::make_error(arg_result.error_message(), 
				arg_result.error_token());
		}
		
		if (auto arg = arg_result.node()) {
			// Check for pack expansion (...) after the argument
			if (ctx.handle_pack_expansion && peek() == "..."_tok) {
				Token ellipsis_token = peek_info();
				advance(); // consume '...'
				
				// Handle simple pack expansion if enabled
				bool expanded = false;
				if (ctx.expand_simple_packs && arg->is<IdentifierNode>()) {
					std::string_view pack_name = arg->as<IdentifierNode>().name();
					
					// Try to find expanded pack elements in the symbol table
					// Pattern: pack_name_0, pack_name_1, etc.
					size_t pack_size = 0;
					StringBuilder sb;
					for (size_t i = 0; i < MAX_PACK_ELEMENTS; ++i) {
						std::string_view element_name = sb
							.append(pack_name)
							.append("_")
							.append(i)
							.preview();
						
						if (gSymbolTable.lookup(element_name).has_value()) {
							++pack_size;
							sb.reset();
						} else {
							break;
						}
					}
					sb.reset();
					
					if (pack_size > 0) {
						// Add each pack element as a separate argument
						for (size_t i = 0; i < pack_size; ++i) {
							std::string_view element_name = StringBuilder()
								.append(pack_name)
								.append("_")
								.append(i)
								.commit();
							
							// Use ellipsis token position for proper error reporting
							Token elem_token(Token::Type::Identifier, element_name, 
								ellipsis_token.line(), ellipsis_token.column(), ellipsis_token.file_index());
							auto elem_node = emplace_node<ExpressionNode>(IdentifierNode(elem_token));
							args.push_back(elem_node);
							
							// Collect type if needed
							if (ctx.collect_types) {
								std::optional<TypeSpecifierNode> elem_type = get_expression_type(elem_node);
								if (elem_type.has_value()) {
									arg_types.push_back(*elem_type);
								} else {
									arg_types.emplace_back(Type::Int, TypeQualifier::None, 32, ellipsis_token);
								}
							}
						}
						expanded = true;
					}
				}
				
				if (!expanded) {
					// Wrap the argument in a PackExpansionExprNode
					auto pack_expr = emplace_node<ExpressionNode>(
						PackExpansionExprNode(*arg, ellipsis_token));
					args.push_back(pack_expr);
					
					// For pack expansions, we can't reliably determine the type
					if (ctx.collect_types) {
						std::optional<TypeSpecifierNode> arg_type = get_expression_type(*arg);
						if (arg_type.has_value()) {
							arg_types.push_back(*arg_type);
						} else {
							arg_types.emplace_back(Type::Int, TypeQualifier::None, 32, ellipsis_token);
						}
					}
				}
				
				FLASH_LOG(Parser, Debug, "Handled pack expansion for function argument");
			} else {
				args.push_back(*arg);
				
				// Collect argument type if requested
				if (ctx.collect_types) {
					std::optional<TypeSpecifierNode> arg_type = get_expression_type(*arg);
					if (arg_type.has_value()) {
						arg_types.push_back(*arg_type);
					} else {
						// Fallback: try to deduce from the expression
						// Use current_token_ for error location since we've just parsed the expression
						Type deduced_type = Type::Int;
						if (arg->is<ExpressionNode>()) {
							const ExpressionNode& expr = arg->as<ExpressionNode>();
							if (std::holds_alternative<NumericLiteralNode>(expr)) {
								deduced_type = std::get<NumericLiteralNode>(expr).type();
							} else if (std::holds_alternative<IdentifierNode>(expr)) {
								const auto& ident = std::get<IdentifierNode>(expr);
								auto symbol = lookup_symbol(StringTable::getOrInternStringHandle(ident.name()));
								if (symbol.has_value()) {
									if (const DeclarationNode* decl = get_decl_from_symbol(*symbol)) {
										deduced_type = decl->type_node().as<TypeSpecifierNode>().type();
									}
								}
							}
						}
						arg_types.emplace_back(deduced_type, TypeQualifier::None, get_type_size_bits(deduced_type), 
							current_token_);
					}
				}
			}
		}
		
		if (peek().is_eof()) {
			return ParsedFunctionArguments::make_error("Expected ',' or ')' in function call", current_token_);
		}
		
		if (peek() == ")"_tok) {
			break;
		}
		
		if (!consume(","_tok)) {
			return ParsedFunctionArguments::make_error("Expected ',' between function arguments", current_token_);
		}
	}
	
	if (ctx.collect_types) {
		return ParsedFunctionArguments::make_success(std::move(args), std::move(arg_types));
	}
	return ParsedFunctionArguments::make_success(std::move(args));
}

// Helper to apply lvalue reference for perfect forwarding deduction
// This is used when collecting argument types for template instantiation.
// In perfect forwarding (T&&), lvalues should deduce to T& while rvalues deduce to T.
std::vector<TypeSpecifierNode> Parser::apply_lvalue_reference_deduction(
	const ChunkedVector<ASTNode>& args, 
	const std::vector<TypeSpecifierNode>& arg_types)
{
	std::vector<TypeSpecifierNode> result;
	result.reserve(arg_types.size());
	
	for (size_t i = 0; i < arg_types.size(); ++i) {
		TypeSpecifierNode arg_type_node = arg_types[i];
		
		// Check if this is an lvalue (for perfect forwarding deduction)
		// Lvalues: named variables, array subscripts, member access, dereferences, string literals
		// Rvalues: numeric/bool literals, temporaries, function calls returning non-reference
		if (i < args.size() && args[i].is<ExpressionNode>()) {
			const ExpressionNode& expr = args[i].as<ExpressionNode>();
			bool is_lvalue = std::visit([](const auto& inner) -> bool {
				using T = std::decay_t<decltype(inner)>;
				if constexpr (std::is_same_v<T, IdentifierNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, ArraySubscriptNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, MemberAccessNode>) {
					return true;
				} else if constexpr (std::is_same_v<T, UnaryOperatorNode>) {
					return inner.op() == "*" || inner.op() == "++" || inner.op() == "--";
				} else if constexpr (std::is_same_v<T, StringLiteralNode>) {
					return true;
				} else {
					return false;
				}
			}, expr);
			
			if (is_lvalue) {
				arg_type_node.set_lvalue_reference(true);
			}
		}
		
		result.push_back(arg_type_node);
	}
	
	return result;
}

// Consume leading specifiers (constexpr, consteval, inline, explicit, virtual) before a member declaration.
// Handles explicit(condition) syntax. Returns a bitmask of MemberLeadingSpecifiers flags.
FlashCpp::MemberLeadingSpecifiers Parser::parse_member_leading_specifiers() {
	using enum FlashCpp::MemberLeadingSpecifiers;
	FlashCpp::MemberLeadingSpecifiers specs = MLS_None;
	while (true) {
		auto k = peek();
		if (k == "constexpr"_tok) {
			specs |= MLS_Constexpr;
			advance();
		} else if (k == "consteval"_tok) {
			specs |= MLS_Consteval;
			advance();
		} else if (k == "inline"_tok) {
			specs |= MLS_Inline;
			advance();
		} else if (k == "explicit"_tok) {
			specs |= MLS_Explicit;
			advance();
			if (peek() == "("_tok) {
				skip_balanced_parens(); // explicit(condition)
			}
		} else if (k == "virtual"_tok) {
			specs |= MLS_Virtual;
			advance();
		} else {
			break;
		}
	}
	return specs;
}

// Phase 2: Unified trailing specifiers parsing
// This method handles all common trailing specifiers after function parameters:
// - CV qualifiers: const, volatile
// - Ref qualifiers: &, &&
// - noexcept specifier: noexcept, noexcept(expr)
// - Virtual specifiers: override, final
// - Special definitions: = 0 (pure virtual), = default, = delete
// - Attributes: __attribute__((...))
ParseResult Parser::parse_function_trailing_specifiers(
	FlashCpp::MemberQualifiers& out_quals,
	FlashCpp::FunctionSpecifiers& out_specs
) {
	// Initialize output structures
	out_quals = FlashCpp::MemberQualifiers{};
	out_specs = FlashCpp::FunctionSpecifiers{};

	while (!peek().is_eof()) {
		const Token& token = peek_info();

		// Parse CV qualifiers (const, volatile)
		if (token.kind() == "const"_tok) {
			out_quals.is_const = true;
			advance();
			continue;
		}
		if (token.kind() == "volatile"_tok) {
			out_quals.is_volatile = true;
			advance();
			continue;
		}

		// Parse ref qualifiers (& and &&)
		if (token.kind() == "&"_tok) {
			advance();
			out_quals.is_lvalue_ref = true;
			continue;
		}
		if (token.kind() == "&&"_tok) {
			advance();
			out_quals.is_rvalue_ref = true;
			continue;
		}

		// Parse noexcept specifier
		if (token.kind() == "noexcept"_tok) {
			advance(); // consume 'noexcept'
			out_specs.is_noexcept = true;

			// Check for noexcept(expr) form
			if (peek() == "("_tok) {
				advance(); // consume '('

				// Parse the constant expression
				auto expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (expr_result.is_error()) {
					return expr_result;
				}

				if (expr_result.node().has_value()) {
					out_specs.noexcept_expr = *expr_result.node();
				}

				if (!consume(")"_tok)) {
					return ParseResult::error("Expected ')' after noexcept expression", current_token_);
				}
			}
			continue;
		}

		// Parse throw() (old-style exception specification) - just skip it
		if (token.kind() == "throw"_tok) {
			advance(); // consume 'throw'
			if (peek() == "("_tok) {
				advance(); // consume '('
				int paren_depth = 1;
				while (!peek().is_eof() && paren_depth > 0) {
					if (peek() == "("_tok) paren_depth++;
					else if (peek() == ")"_tok) paren_depth--;
					advance();
				}
			}
			continue;
		}

		// Parse requires clause - skip the constraint expression
		// Pattern: func() noexcept requires constraint { }
		// Also handles: requires requires { expr } (nested requires expression)
		if (token.kind() == "requires"_tok) {
			advance(); // consume 'requires'
			
			// Skip the constraint expression by counting balanced brackets/parens
			// The constraint expression ends before '{', ';', '= default', '= delete', or '= 0'
			// BUT: If the constraint is a requires-expression, its body uses { } which shouldn't end the clause
			int paren_depth = 0;
			int angle_depth = 0;
			int brace_depth = 0;
			while (!peek().is_eof()) {
				auto tk = peek();
				
				// Special handling for 'requires' keyword inside the constraint
				// This indicates a requires-expression like: requires requires { ... }
				// The { } after a nested 'requires' is the requires-expression body, not the function body
				if (tk == "requires"_tok) {
					advance(); // consume nested 'requires'
					// Skip optional parameter list: requires(const T t) { ... }
					if (peek() == "("_tok) {
						advance(); // consume '('
						int param_paren_depth = 1;
						while (!peek().is_eof() && param_paren_depth > 0) {
							if (peek() == "("_tok) param_paren_depth++;
							else if (peek() == ")"_tok) param_paren_depth--;
							advance();
						}
					}
					// Expect the requires-expression body
					if (peek() == "{"_tok) {
						advance(); // consume '{'
						brace_depth++;
					}
					continue;
				}
				
				// At top level, check for end of constraint BEFORE updating depth tracking
				// This ensures we break on the function body '{' instead of consuming it
				if (paren_depth == 0 && angle_depth == 0 && brace_depth == 0) {
					// Body start or end of declaration
					if (tk == "{"_tok || tk == ";"_tok) {
						break;
					}
					// Check for = default, = delete, = 0
					if (tk == "="_tok) {
						break;
					}
				}
				
				// Track nested brackets (after checking for end of constraint)
				if (tk == "("_tok) paren_depth++;
				else if (tk == ")"_tok) paren_depth--;
				else if (tk == "{"_tok) brace_depth++;
				else if (tk == "}"_tok) brace_depth--;
				else update_angle_depth(tk, angle_depth);
				
				advance();
			}
			continue;
		}

		// Parse override/final
		// Note: 'override' and 'final' are contextual keywords in C++11+
		// They may be tokenized as either Keyword or Identifier depending on context
		// We accept both to be safe
		if (token.kind() == "override"_tok || 
		    (token.type() == Token::Type::Identifier && token.value() == "override")) {
			out_specs.is_override = true;
			advance();
			continue;
		}
		if (token.kind() == "final"_tok ||
		    (token.type() == Token::Type::Identifier && token.value() == "final")) {
			out_specs.is_final = true;
			advance();
			continue;
		}

		// Parse = 0 (pure virtual), = default, = delete
		if (token.kind() == "="_tok) {
			auto next_kind = peek(1);
			if (next_kind.is_literal()) {
				// Check for "= 0" (pure virtual) — need string check for "0"
				if (peek_info(1).value() == "0") {
					advance(); // consume '='
					advance(); // consume '0'
					out_specs.is_pure_virtual = true;
					continue;
				}
			}
			if (next_kind == "default"_tok) {
				advance(); // consume '='
				advance(); // consume 'default'
				out_specs.is_defaulted = true;
				continue;
			}
			if (next_kind == "delete"_tok) {
				advance(); // consume '='
				advance(); // consume 'delete'
				out_specs.is_deleted = true;
				continue;
			}
			// '=' followed by something else - not a trailing specifier
			break;
		}

		// Parse __attribute__((...))
		// Note: __attribute__ is an identifier, not a keyword — string compare required
		if (token.type() == Token::Type::Identifier && token.value() == "__attribute__") {
			skip_gcc_attributes();
			continue;
		}

		// Not a trailing specifier, stop
		break;
	}

	return ParseResult::success();
}

// Phase 4: Unified function header parsing
// This method parses the complete function header (return type, name, parameters, trailing specifiers)
// in a unified way across all function types (free functions, member functions, constructors, etc.)
ParseResult Parser::parse_function_header(
	const FlashCpp::FunctionParsingContext& ctx,
	FlashCpp::ParsedFunctionHeader& out_header
) {
	// Initialize output header
	out_header = FlashCpp::ParsedFunctionHeader{};

	// Parse return type (if not constructor/destructor)
	if (ctx.kind != FlashCpp::FunctionKind::Constructor && 
	    ctx.kind != FlashCpp::FunctionKind::Destructor) {
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		if (type_result.node().has_value() && type_result.node()->is<TypeSpecifierNode>()) {
			// Store pointer to the type node
			out_header.return_type = &type_result.node()->as<TypeSpecifierNode>();
		}
	}

	// Parse function name
	// Note: For operators, we need special handling
	if (ctx.kind == FlashCpp::FunctionKind::Operator || ctx.kind == FlashCpp::FunctionKind::Conversion) {
		// Operator parsing is complex - for now, just check for 'operator' keyword
		if (peek() == "operator"_tok) {
			out_header.name_token = peek_info();
			advance();
			// Operator symbol parsing would continue here in full implementation
		} else {
			return ParseResult::error("Expected 'operator' keyword", current_token_);
		}
	} else if (ctx.kind == FlashCpp::FunctionKind::Constructor) {
		// Constructor name must match the parent struct name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected constructor name", current_token_);
		}
		if (peek_info().value() != ctx.parent_struct_name) {
			return ParseResult::error("Constructor name must match class name", peek_info());
		}
		out_header.name_token = peek_info();
		advance();
	} else if (ctx.kind == FlashCpp::FunctionKind::Destructor) {
		// Destructor must start with '~'
		if (peek() != "~"_tok) {
			return ParseResult::error("Expected '~' for destructor", current_token_);
		}
		advance();  // consume '~'
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected destructor name", current_token_);
		}
		if (peek_info().value() != ctx.parent_struct_name) {
			return ParseResult::error("Destructor name must match class name", peek_info());
		}
		out_header.name_token = peek_info();
		advance();
	} else {
		// Regular function name
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected function name", current_token_);
		}
		out_header.name_token = peek_info();
		advance();
	}

	// Parse parameter list using Phase 1 unified method
	auto params_result = parse_parameter_list(out_header.params, out_header.storage.calling_convention);
	if (params_result.is_error()) {
		return params_result;
	}

	// Parse trailing specifiers using Phase 2 unified method
	auto specs_result = parse_function_trailing_specifiers(out_header.member_quals, out_header.specifiers);
	if (specs_result.is_error()) {
		return specs_result;
	}

	// Validate specifiers for function kind
	if (ctx.kind == FlashCpp::FunctionKind::Free) {
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Free functions cannot be virtual", out_header.name_token);
		}
		if (out_header.specifiers.is_override || out_header.specifiers.is_final) {
			return ParseResult::error("Free functions cannot use override/final", out_header.name_token);
		}
		if (out_header.specifiers.is_pure_virtual) {
			return ParseResult::error("Free functions cannot be pure virtual", out_header.name_token);
		}
		// CV qualifiers don't apply to free functions
		if (out_header.member_quals.is_const || out_header.member_quals.is_volatile) {
			return ParseResult::error("Free functions cannot have const/volatile qualifiers", out_header.name_token);
		}
	}

	if (ctx.kind == FlashCpp::FunctionKind::StaticMember) {
		// Static member functions can't be virtual or have CV qualifiers
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Static member functions cannot be virtual", out_header.name_token);
		}
		if (out_header.member_quals.is_const || out_header.member_quals.is_volatile) {
			return ParseResult::error("Static member functions cannot have const/volatile qualifiers", out_header.name_token);
		}
	}

	if (ctx.kind == FlashCpp::FunctionKind::Constructor) {
		// Constructors can't be virtual, override, final, or have return type
		if (out_header.specifiers.is_virtual) {
			return ParseResult::error("Constructors cannot be virtual", out_header.name_token);
		}
		if (out_header.specifiers.is_override || out_header.specifiers.is_final) {
			return ParseResult::error("Constructors cannot use override/final", out_header.name_token);
		}
	}

	// Parse trailing return type if present (for auto return type)
	if (peek() == "->"_tok) {
		advance();  // consume '->'
		auto trailing_result = parse_type_specifier();
		if (trailing_result.is_error()) {
			return trailing_result;
		}
		
		// Apply pointer and reference qualifiers (e.g., T*, T&, T&&)
		if (trailing_result.node().has_value() && trailing_result.node()->is<TypeSpecifierNode>()) {
			TypeSpecifierNode& type_spec = trailing_result.node()->as<TypeSpecifierNode>();
			
			consume_pointer_ref_modifiers(type_spec);
		}
		
		out_header.trailing_return_type = trailing_result.node();
	}

	return ParseResult::success();
}

// Phase 4: Create a FunctionDeclarationNode from a ParsedFunctionHeader
// This bridges the unified header parsing with the existing AST node creation
ParseResult Parser::create_function_from_header(
	const FlashCpp::ParsedFunctionHeader& header,
	[[maybe_unused]] const FlashCpp::FunctionParsingContext& ctx
) {
	// Create the type specifier node for the return type
	ASTNode type_node;
	if (header.return_type != nullptr) {
		type_node = ASTNode::emplace_node<TypeSpecifierNode>(*header.return_type);
	} else {
		// For constructors/destructors, create a void return type
		type_node = ASTNode::emplace_node<TypeSpecifierNode>(Type::Void, 0, 0, Token());
	}

	// Create the declaration node with type and name
	auto [decl_node, decl_ref] = emplace_node_ref<DeclarationNode>(type_node, header.name_token);

	// Create the function declaration node using the DeclarationNode reference
	auto [func_node, func_ref] = emplace_node_ref<FunctionDeclarationNode>(decl_ref);

	// Set calling convention
	func_ref.set_calling_convention(header.storage.calling_convention);

	// Set linkage
	if (header.storage.linkage != Linkage::None) {
		func_ref.set_linkage(header.storage.linkage);
	} else if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	} else {
		// Check if there's a forward declaration with linkage and inherit it
		// Use lookup_all to check all overloads in case there are multiple
		auto all_overloads = gSymbolTable.lookup_all(header.name_token.value());
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const auto& forward_decl = overload.as<FunctionDeclarationNode>();
				if (forward_decl.linkage() != Linkage::None) {
					func_ref.set_linkage(forward_decl.linkage());
					break;  // Found a forward declaration with linkage, use it
				}
			}
		}
	}

	// Add parameters
	for (const auto& param : header.params.parameters) {
		func_ref.add_parameter_node(param);
	}
	func_ref.set_is_variadic(header.params.is_variadic);

	// Set noexcept if specified
	if (header.specifiers.is_noexcept) {
		func_ref.set_noexcept(true);
		if (header.specifiers.noexcept_expr.has_value()) {
			func_ref.set_noexcept_expression(*header.specifiers.noexcept_expr);
		}
	}

	// Set constexpr/consteval
	func_ref.set_is_constexpr(header.storage.is_constexpr);
	func_ref.set_is_consteval(header.storage.is_consteval);

	return func_node;
}

// Phase 5: Unified function body parsing
// This method handles all the common body parsing logic including:
// - = default handling
// - = delete handling
// - Declaration-only (no body)
// - Scope setup with RAII guards
// - 'this' pointer injection for member functions
// - Parameter registration
// - Block parsing
ParseResult Parser::parse_function_body_with_context(
	const FlashCpp::FunctionParsingContext& ctx,
	const FlashCpp::ParsedFunctionHeader& header,
	std::optional<ASTNode>& out_body
) {
	// Initialize output
	out_body = std::nullopt;

	// Handle = default
	if (header.specifiers.is_defaulted) {
		auto [block_node, block_ref] = create_node_ref(BlockNode());
		out_body = block_node;
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Handle = delete
	if (header.specifiers.is_deleted) {
		// No body for deleted functions
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Handle pure virtual (= 0)
	if (header.specifiers.is_pure_virtual) {
		// No body for pure virtual functions
		// Note: semicolon should already be consumed by the caller after parsing specifiers
		return ParseResult::success();
	}

	// Check for declaration only (no body) - semicolon
	if (peek() == ";"_tok) {
		advance();  // consume ';'
		return ParseResult::success();  // Declaration only, no body
	}

	// Expect function body with '{'
	if (peek() != "{"_tok) {
		return ParseResult::error("Expected '{' or ';' after function declaration", current_token_);
	}

	// Set up function scope using RAII guard (Phase 3)
	FlashCpp::SymbolTableScope func_scope(ScopeType::Function);

	// Inject 'this' pointer for member functions, constructors, and destructors
	if (ctx.kind == FlashCpp::FunctionKind::Member ||
	    ctx.kind == FlashCpp::FunctionKind::Constructor ||
	    ctx.kind == FlashCpp::FunctionKind::Destructor) {
		// Find the parent struct type
		auto type_it = gTypesByName.find(StringTable::getOrInternStringHandle(ctx.parent_struct_name));
		if (type_it != gTypesByName.end()) {
			// Create 'this' pointer type: StructName*
			auto [this_type_node, this_type_ref] = emplace_node_ref<TypeSpecifierNode>(
				Type::Struct, type_it->second->type_index_,
				64,  // Pointer size in bits
				Token()
			);
			this_type_ref.add_pointer_level();

			// Create a declaration node for 'this'
			Token this_token(Token::Type::Keyword, "this"sv, 0, 0, 0);
			auto [this_decl_node, this_decl_ref] = emplace_node_ref<DeclarationNode>(this_type_node, this_token);

			// Insert 'this' into the symbol table
			gSymbolTable.insert("this"sv, this_decl_node);
		}
	}

	// Register parameters in the symbol table
	for (const auto& param : header.params.parameters) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl_node = param.as<DeclarationNode>();
			const Token& param_token = param_decl_node.identifier_token();
			gSymbolTable.insert(param_token.value(), param);
		}
	}

	// Parse the block
	auto block_result = parse_block();
	if (block_result.is_error()) {
		return block_result;
	}

	if (block_result.node().has_value()) {
		out_body = *block_result.node();
	}

	// func_scope automatically exits scope when destroyed

	return ParseResult::success();
}

// Phase 5: Helper method to register member functions in the symbol table
// This implements C++20's complete-class context for inline member function bodies
void Parser::register_member_functions_in_scope(StructDeclarationNode* struct_node, size_t struct_type_index) {
	// Add member functions from the struct itself
	if (struct_node) {
		for (const auto& member_func : struct_node->member_functions()) {
			if (member_func.function_declaration.is<FunctionDeclarationNode>()) {
				const auto& func_decl = member_func.function_declaration.as<FunctionDeclarationNode>();
				gSymbolTable.insert(func_decl.decl_node().identifier_token().value(), member_func.function_declaration);
			}
		}
	}

	// Also add inherited member functions from base classes
	if (struct_type_index < gTypeInfo.size()) {
		const TypeInfo& type_info = gTypeInfo[struct_type_index];
		const StructTypeInfo* struct_info = type_info.getStructInfo();
		if (struct_info) {
			std::vector<TypeIndex> base_classes_to_search;
			for (const auto& base : struct_info->base_classes) {
				base_classes_to_search.push_back(base.type_index);
			}
			for (size_t i = 0; i < base_classes_to_search.size(); ++i) {
				TypeIndex base_idx = base_classes_to_search[i];
				if (base_idx >= gTypeInfo.size()) continue;
				const TypeInfo& base_type_info = gTypeInfo[base_idx];
				const StructTypeInfo* base_struct_info = base_type_info.getStructInfo();
				if (!base_struct_info) continue;
				for (const auto& member_func : base_struct_info->member_functions) {
					if (member_func.function_decl.is<FunctionDeclarationNode>()) {
						gSymbolTable.insert(StringTable::getStringView(member_func.getName()), member_func.function_decl);
					}
				}
				for (const auto& nested_base : base_struct_info->base_classes) {
					bool already_in_list = false;
					for (TypeIndex existing : base_classes_to_search) {
						if (existing == nested_base.type_index) { already_in_list = true; break; }
					}
					if (!already_in_list) base_classes_to_search.push_back(nested_base.type_index);
				}
			}
		}
	}
}

// Phase 5: Helper method to set up member function context and scope
void Parser::setup_member_function_context(StructDeclarationNode* struct_node, StringHandle struct_name, size_t struct_type_index) {
	// Push member function context
	member_function_context_stack_.push_back({
		struct_name,
		struct_type_index,
		struct_node,
		nullptr  // local_struct_info - not needed here since TypeInfo should be available
	});

	// Register member functions in symbol table for complete-class context
	register_member_functions_in_scope(struct_node, struct_type_index);
}

// Phase 5: Helper to register function parameters in the symbol table
void Parser::register_parameters_in_scope(const std::vector<ASTNode>& params) {
	for (const auto& param : params) {
		if (param.is<DeclarationNode>()) {
			const auto& param_decl = param.as<DeclarationNode>();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		} else if (param.is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& var_decl = param.as<VariableDeclarationNode>();
			const DeclarationNode& param_decl = var_decl.declaration();
			gSymbolTable.insert(param_decl.identifier_token().value(), param);
		}
	}
}

// Phase 5: Unified delayed function body parsing
ParseResult Parser::parse_delayed_function_body(DelayedFunctionBody& delayed, std::optional<ASTNode>& out_body) {
	out_body = std::nullopt;
	
	// Enter function scope
	gSymbolTable.enter_scope(ScopeType::Function);
	
	// Set up member function context
	setup_member_function_context(delayed.struct_node, delayed.struct_name, delayed.struct_type_index);
	
	// Get the appropriate function node and parameters
	FunctionDeclarationNode* func_node = nullptr;
	const std::vector<ASTNode>* params = nullptr;
	
	if (delayed.is_constructor && delayed.ctor_node) {
		current_function_ = nullptr;  // Constructors don't have return type
		params = &delayed.ctor_node->parameter_nodes();
	} else if (delayed.is_destructor && delayed.dtor_node) {
		current_function_ = nullptr;  // Destructors don't have return type
		// Destructors have no parameters
	} else if (delayed.func_node) {
		func_node = delayed.func_node;
		current_function_ = func_node;
		params = &func_node->parameter_nodes();
	}
	
	// Register parameters in symbol table
	if (params) {
		register_parameters_in_scope(*params);
	}
	
	// Parse constructor initializer list if present (for constructors with delayed parsing)
	if (delayed.is_constructor && delayed.has_initializer_list && delayed.ctor_node) {
		// Restore to the position of the initializer list (':')
		restore_token_position(delayed.initializer_list_start);
		
		// Parse the initializer list now that all class members are visible
		if (peek() == ":"_tok) {
			advance();  // consume ':'

			// Parse initializers until we hit '{' or ';'
			while (peek() != "{"_tok &&
			       peek() != ";"_tok) {
				// Parse initializer name (could be base class or member)
				auto init_name_token = advance();
				if (init_name_token.type() != Token::Type::Identifier) {
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return ParseResult::error("Expected member or base class name in initializer list", init_name_token);
				}

				std::string_view init_name = init_name_token.value();

				// Check for template arguments: Base<T>(...) in base class initializer
				if (peek() == "<"_tok) {
					skip_template_arguments();
				}

				// Expect '(' or '{'
				bool is_paren = peek() == "("_tok;
				bool is_brace = peek() == "{"_tok;

				if (!is_paren && !is_brace) {
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return ParseResult::error("Expected '(' or '{' after initializer name", peek_info());
				}

				advance();  // consume '(' or '{'
				TokenKind close_kind = [is_paren]() { if (is_paren) return ")"_tok; return "}"_tok; }();

				// Parse initializer arguments
				std::vector<ASTNode> init_args;
				if (peek().is_eof() || peek() != close_kind) {
					do {
						ParseResult arg_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (arg_result.is_error()) {
							// Clean up
							current_function_ = nullptr;
							member_function_context_stack_.pop_back();
							gSymbolTable.exit_scope();
							return arg_result;
						}
						if (auto arg_node = arg_result.node()) {
							// Check for pack expansion: expr...
							if (peek() == "..."_tok) {
								advance(); // consume '...'
								// Mark this as a pack expansion - actual expansion happens at instantiation
							}
							init_args.push_back(*arg_node);
						}
					} while (consume(","_tok));
				}

				// Expect closing delimiter
				if (!consume(close_kind)) {
					// Clean up
					current_function_ = nullptr;
					member_function_context_stack_.pop_back();
					gSymbolTable.exit_scope();
					return ParseResult::error(is_paren ? "Expected ')' after initializer arguments"
					                                   : "Expected '}' after initializer arguments", peek_info());
				}

				// Determine if this is a delegating, base class, or member initializer
				bool is_delegating = (init_name == delayed.struct_name);
				bool is_base_init = false;
				
				if (is_delegating) {
					// Delegating constructor: Point() : Point(0, 0) {}
					// In C++11, if a constructor delegates, it CANNOT have other initializers
					if (!delayed.ctor_node->member_initializers().empty() || !delayed.ctor_node->base_initializers().empty()) {
						// Clean up
						current_function_ = nullptr;
						member_function_context_stack_.pop_back();
						gSymbolTable.exit_scope();
						return ParseResult::error("Delegating constructor cannot have other member or base initializers", init_name_token);
					}
					delayed.ctor_node->set_delegating_initializer(std::move(init_args));
				} else {
					// Check if it's a base class initializer
					if (delayed.struct_node) {
						for (const auto& base : delayed.struct_node->base_classes()) {
							if (base.name == init_name) {
								is_base_init = true;
								// Phase 7B: Intern base class name and use StringHandle overload
								StringHandle base_name_handle = StringTable::getOrInternStringHandle(init_name);
								delayed.ctor_node->add_base_initializer(base_name_handle, std::move(init_args));
								break;
							}
						}
						// Also check deferred template base classes (e.g., Base<T> in template<T> struct Derived : Base<T>)
						if (!is_base_init) {
							StringHandle init_name_handle = StringTable::getOrInternStringHandle(init_name);
							for (const auto& deferred_base : delayed.struct_node->deferred_template_base_classes()) {
								if (deferred_base.base_template_name == init_name_handle) {
									is_base_init = true;
									delayed.ctor_node->add_base_initializer(init_name_handle, std::move(init_args));
									break;
								}
							}
						}
					}

					if (!is_base_init) {
						// It's a member initializer
						// For simplicity, we'll use the first argument as the initializer expression
						if (!init_args.empty()) {
							delayed.ctor_node->add_member_initializer(init_name, init_args[0]);
						}
					}
				}

				// Check for comma (more initializers) or '{'/';' (end of initializer list)
				if (!consume(","_tok)) {
					// No comma, so we expect '{' or ';' next
					break;
				}
			}
		}
		
		// After parsing initializer list, restore to the body position
		restore_token_position(delayed.body_start);
	}
	
	// Parse the function body
	auto block_result = parse_block();
	if (block_result.is_error()) {
		// Clean up
		current_function_ = nullptr;
		member_function_context_stack_.pop_back();
		gSymbolTable.exit_scope();
		return block_result;
	}
	
	// Set the body on the appropriate node
	if (block_result.node().has_value()) {
		out_body = *block_result.node();
		if (delayed.is_constructor && delayed.ctor_node) {
			delayed.ctor_node->set_definition(*block_result.node());
		} else if (delayed.is_destructor && delayed.dtor_node) {
			delayed.dtor_node->set_definition(*block_result.node());
		} else if (delayed.func_node) {
			delayed.func_node->set_definition(*block_result.node());
			// Deduce auto return types from function body (only if return type is auto)
			TypeSpecifierNode return_type = delayed.func_node->decl_node().type_node().as<TypeSpecifierNode>();
			if (return_type.type() == Type::Auto) {
				deduce_and_update_auto_return_type(*delayed.func_node);
			}
		}
	}
	
	// Clean up context
	current_function_ = nullptr;
	member_function_context_stack_.pop_back();
	gSymbolTable.exit_scope();
	
	return ParseResult::success();
}

// Phase 7: Unified signature validation for out-of-line definitions
// Compares a declaration's signature with a definition's signature and returns detailed mismatch information
FlashCpp::SignatureValidationResult Parser::validate_signature_match(
	const FunctionDeclarationNode& declaration,
	const FunctionDeclarationNode& definition)
{
	using namespace FlashCpp;
	
	// Helper lambda to extract TypeSpecifierNode from a parameter
	auto extract_param_type = [](const ASTNode& param) -> const TypeSpecifierNode* {
		if (param.is<DeclarationNode>()) {
			return &param.as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
		} else if (param.is<VariableDeclarationNode>()) {
			return &param.as<VariableDeclarationNode>().declaration().type_node().as<TypeSpecifierNode>();
		}
		return nullptr;
	};
	
	// Validate parameter count
	const auto& decl_params = declaration.parameter_nodes();
	const auto& def_params = definition.parameter_nodes();
	
	if (decl_params.size() != def_params.size()) {
		std::string msg = "Declaration has " + std::to_string(decl_params.size()) +
		                  " parameters, definition has " + std::to_string(def_params.size());
		return SignatureValidationResult::error(SignatureMismatch::ParameterCount, 0, std::move(msg));
	}
	
	// Validate each parameter type
	for (size_t i = 0; i < decl_params.size(); ++i) {
		const TypeSpecifierNode* decl_type = extract_param_type(decl_params[i]);
		const TypeSpecifierNode* def_type = extract_param_type(def_params[i]);
		
		if (!decl_type || !def_type) {
			return SignatureValidationResult::error(SignatureMismatch::InternalError, i + 1,
				"Unable to extract parameter type information");
		}
		
		// Compare basic type properties (ignore top-level cv-qualifiers on parameters - they don't affect signature)
		if (def_type->type() != decl_type->type() ||
			def_type->type_index() != decl_type->type_index() ||
			def_type->pointer_depth() != decl_type->pointer_depth() ||
			def_type->is_reference() != decl_type->is_reference()) {
			std::string msg = "Parameter " + std::to_string(i + 1) + " type mismatch";
			return SignatureValidationResult::error(SignatureMismatch::ParameterType, i + 1, std::move(msg));
		}
		
		// For pointers, compare cv-qualifiers on pointed-to type (int* vs const int*)
		if (def_type->pointer_depth() > 0) {
			if (def_type->cv_qualifier() != decl_type->cv_qualifier()) {
				std::string msg = "Parameter " + std::to_string(i + 1) + " pointer cv-qualifier mismatch";
				return SignatureValidationResult::error(SignatureMismatch::ParameterCVQualifier, i + 1, std::move(msg));
			}
			
			// cv-qualifiers on pointer levels also matter: int* const vs int*
			const auto& def_levels = def_type->pointer_levels();
			const auto& decl_levels = decl_type->pointer_levels();
			for (size_t p = 0; p < def_levels.size(); ++p) {
				if (def_levels[p].cv_qualifier != decl_levels[p].cv_qualifier) {
					std::string msg = "Parameter " + std::to_string(i + 1) + " pointer level cv-qualifier mismatch";
					return SignatureValidationResult::error(SignatureMismatch::ParameterPointerLevel, i + 1, std::move(msg));
				}
			}
		}
		
		// For references, compare cv-qualifiers on the base type (const T& vs T&)
		if (def_type->is_reference()) {
			if (def_type->cv_qualifier() != decl_type->cv_qualifier()) {
				std::string msg = "Parameter " + std::to_string(i + 1) + " reference cv-qualifier mismatch";
				return SignatureValidationResult::error(SignatureMismatch::ParameterCVQualifier, i + 1, std::move(msg));
			}
		}
	}
	
	// Validate return type
	const DeclarationNode& decl_decl = declaration.decl_node();
	const DeclarationNode& def_decl = definition.decl_node();
	const TypeSpecifierNode& decl_return_type = decl_decl.type_node().as<TypeSpecifierNode>();
	const TypeSpecifierNode& def_return_type = def_decl.type_node().as<TypeSpecifierNode>();
	
	if (def_return_type.type() != decl_return_type.type() ||
		def_return_type.type_index() != decl_return_type.type_index() ||
		def_return_type.pointer_depth() != decl_return_type.pointer_depth() ||
		def_return_type.is_reference() != decl_return_type.is_reference()) {
		return SignatureValidationResult::error(SignatureMismatch::ReturnType, 0, "Return type mismatch");
	}
	
	return SignatureValidationResult::success();
}

// Phase 6 (mangling): Generate and set mangled name on a FunctionDeclarationNode
// This should be called after all function properties are set (parameters, variadic flag, etc.)
// Note: The mangled name is stored as a string_view pointing to ChunkedStringAllocator storage
// which remains valid for the lifetime of the compilation.
void Parser::compute_and_set_mangled_name(FunctionDeclarationNode& func_node)
{
	// Skip if already has a mangled name
	if (func_node.has_mangled_name()) {
		return;
	}
	
	// C linkage functions don't get mangled - just use the function name as-is
	if (func_node.linkage() == Linkage::C) {
		const DeclarationNode& decl_node = func_node.decl_node();
		std::string_view func_name = decl_node.identifier_token().value();
		func_node.set_mangled_name(func_name);
		return;
	}

	// Build namespace path from current symbol table state as string_view vector
	// For member functions, only build namespace path if parent_struct_name doesn't already contain namespace
	// (to avoid double-encoding the namespace in the mangled name)
	std::vector<std::string_view> ns_path;
	bool should_get_namespace = true;
	
	if (func_node.is_member_function()) {
		std::string_view parent_name = func_node.parent_struct_name();
		// If parent_struct_name already contains "::", namespace is embedded in struct name
		// so we don't need to pass it separately
		if (parent_name.find("::") != std::string_view::npos) {
			should_get_namespace = false;
		}
	}
	
	if (should_get_namespace) {
		NamespaceHandle current_handle = gSymbolTable.get_current_namespace_handle();
		std::string_view qualified_namespace = gNamespaceRegistry.getQualifiedName(current_handle);
		ns_path = splitQualifiedNamespace(qualified_namespace);
	}
	
	// Generate the mangled name using the NameMangling helper
	NameMangling::MangledName mangled = NameMangling::generateMangledNameFromNode(func_node, ns_path);
	
	// Set the mangled name on the node
	func_node.set_mangled_name(mangled.view());
}

ParseResult Parser::parse_function_declaration(DeclarationNode& declaration_node, CallingConvention calling_convention)
{
	// Create the function declaration first
	auto [func_node, func_ref] =
		create_node_ref<FunctionDeclarationNode>(declaration_node);
	
	// Set calling convention immediately so it's available during parameter parsing
	func_ref.set_calling_convention(calling_convention);

	// Set linkage from current context (for extern "C" blocks)
	if (current_linkage_ != Linkage::None) {
		func_ref.set_linkage(current_linkage_);
	}

	// Use unified parameter list parsing (Phase 1)
	FlashCpp::ParsedParameterList params;
	auto param_result = parse_parameter_list(params, calling_convention);
	if (param_result.is_error()) {
		return param_result;
	}

	// Apply the parsed parameters to the function
	for (const auto& param : params.parameters) {
		func_ref.add_parameter_node(param);
	}
	func_ref.set_is_variadic(params.is_variadic);

	// If linkage wasn't set from current context, check if there's a forward declaration with linkage
	if (func_ref.linkage() == Linkage::None) {
		// Use lookup_all to check all overloads in case there are multiple
		auto all_overloads = gSymbolTable.lookup_all(declaration_node.identifier_token().value());
		for (const auto& overload : all_overloads) {
			if (overload.is<FunctionDeclarationNode>()) {
				const auto& forward_decl = overload.as<FunctionDeclarationNode>();
				if (forward_decl.linkage() != Linkage::None) {
					func_ref.set_linkage(forward_decl.linkage());
					break;  // Found a forward declaration with linkage, use it
				}
			}
		}
	}

	// Note: Trailing specifiers (const, volatile, &, &&, noexcept, override, final, 
	// = 0, = default, = delete, __attribute__) are NOT handled here.
	// Each call site is responsible for handling trailing specifiers as appropriate:
	// - Free functions: call skip_function_trailing_specifiers() or parse_function_trailing_specifiers()
	// - Member functions: the struct member parsing handles these with full semantic information

	return func_node;
}

