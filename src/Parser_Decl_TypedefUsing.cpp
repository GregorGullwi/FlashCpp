ParseResult Parser::parse_member_type_alias(std::string_view keyword, StructDeclarationNode* struct_ref, AccessSpecifier current_access)
{
	advance(); // consume 'typedef' or 'using'
	
	// For 'using', check if it's an alias or a using-declaration
	if (keyword == "using") {
		auto alias_token = peek_info();
		if (!alias_token.kind().is_identifier()) {
			return ParseResult::error("Expected alias name after 'using'", peek_info());
		}
		
		// Look ahead to see if this is:
		// 1. Type alias: using Alias = Type;  (identifier followed by '=')
		// 2. Using-declaration: using namespace::member;  (identifier followed by '::')
		// 3. Inheriting constructor: using Base<T>::Base;  (identifier<template args> followed by '::')
		SaveHandle lookahead_pos = save_token_position();
		advance(); // consume first identifier
		
		// Skip template arguments if present
		if (peek() == "<"_tok) {
			skip_template_arguments();
		}
		
		auto next_token = peek_info();
		
		if (next_token.kind() == "::"_tok) {
			// This is a using-declaration like: using std::__is_integer<_Tp>::__value;
			// Or an inheriting constructor like: using Base<T>::Base;
			// Parse and extract the member name to register it in the current scope
			std::string_view base_class_name = alias_token.value();  // Remember the first identifier (base class name)
			std::string_view member_name;
			
			while (peek() == "::"_tok) {
				advance(); // consume '::'
				
				// Consume the next identifier, operator, or template
				if (!peek().is_eof()) {
					if (peek().is_identifier()) {
						member_name = peek_info().value();  // Track last identifier as potential member name
						advance(); // consume identifier
						
						// Check for template arguments
						if (peek() == "<"_tok) {
							skip_template_arguments();
							// After template args, the member name is whatever comes next
							member_name = "";  // Reset - next identifier after :: will be the member
						}
					} else if (peek() == "operator"_tok) {
						// using Base::operator Type; (conversion operator)
						// using Base::operator=; (assignment operator)
						advance(); // consume 'operator'
						// Build the full operator name: "operator=", "operator<<", "operator __integral_type", etc.
						StringBuilder op_name_builder;
						op_name_builder.append("operator");
						while (!peek().is_eof() && peek() != ";"_tok && peek() != "..."_tok) {
							// Add space before type names but not before operator symbols
							if (peek().is_identifier() || peek().is_keyword()) {
								op_name_builder.append(" ");
							}
							op_name_builder.append(peek_info().value());
							advance();
						}
						member_name = op_name_builder.commit();
						break;
					} else {
						break;
					}
				}
			}
			
			// Check if this is an inheriting constructor: using Base::Base;
			// Per C++ standard, inheriting constructors specifically require the member name
			// to match the base class name. General using-declarations can import any member.
			// Example: using Base<T>::Base;  // Inherits all Base constructors
			//          using Base::member;     // Imports a specific member
			bool is_inheriting_constructor = (member_name == base_class_name);
			
			// Register the imported member name in the struct parsing context
			// This makes the member accessible by its simple name even when the
			// base class is a dependent type (template) that can't be resolved yet
			if (!member_name.empty()) {
				if (!struct_parsing_context_stack_.empty()) {
					StringHandle member_handle = StringTable::getOrInternStringHandle(member_name);
					struct_parsing_context_stack_.back().imported_members.push_back(member_handle);
					
					if (is_inheriting_constructor) {
						FLASH_LOG(Parser, Debug, "Inheriting constructors from '", base_class_name, "' into struct parsing context");
						// For inheriting constructors, we import the constructors from the base class
						// Mark that constructors are inherited
						struct_parsing_context_stack_.back().has_inherited_constructors = true;
					} else {
						FLASH_LOG(Parser, Debug, "Using-declaration imports member '", member_name, "' into struct parsing context");
					}
				}
			}
			
			// Consume pack expansion '...' if present (C++17 using-declaration with pack expansion)
			// e.g., using Base<Args>::member...;
			if (peek() == "..."_tok) {
				advance(); // consume '...'
			}
			
			// Consume trailing semicolon
			if (peek() == ";"_tok) {
				advance(); // consume ';'
			}
			
			// Discard the saved position - we successfully parsed the using-declaration
			discard_saved_token(lookahead_pos);
			return ParseResult::success();
		}
		
		// Restore position - this is a type alias
		restore_token_position(lookahead_pos);
		
		StringHandle alias_name = alias_token.handle();
		advance(); // consume alias name
		
		// Skip C++ [[...]] and GCC __attribute__((...)) between alias name and '='
		// e.g., using is_always_equal __attribute__((__deprecated__("..."))) = true_type;
		// e.g., using result_type [[__deprecated__]] = size_t;
		skip_cpp_attributes();
		
		// Check for '='
		if (peek() != "="_tok) {
			return ParseResult::error("Expected '=' after alias name", current_token_);
		}
		advance(); // consume '='
		
		// Parse the type
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}
		
		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type after '=' in type alias", current_token_);
		}

		// Parse pointer/reference modifiers after the base type
		// For example: using type = _Tp&; or using RvalueRef = T&&;
		TypeSpecifierNode& type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Parse postfix cv-qualifiers: _Tp const, _Tp volatile, _Tp const volatile
		// This is the C++ postfix const/volatile syntax used in standard library headers
		CVQualifier cv_qualifier = parse_cv_qualifiers();
		type_spec.add_cv_qualifier(cv_qualifier);
		
		// Check for pointer-to-member type syntax: Type Class::*
		// This is used in <type_traits> for result_of patterns
		// Pattern: using _MemPtr = _Res _Class::*;
		if (peek().is_identifier()) {
			// Look ahead to see if this is Class::* pattern
			auto saved_pos = save_token_position();
			Token class_token = peek_info();
			advance(); // consume potential class name
			
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					// This is a pointer-to-member type: Type Class::*
					// Mark the type as a pointer-to-member
					type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
					type_spec.set_member_class_name(class_token.handle());
					FLASH_LOG(Parser, Debug, "Parsed pointer-to-member type: ", type_spec.token().value(), " ", class_token.value(), "::*");
					discard_saved_token(saved_pos);
				} else {
					// Not a pointer-to-member, restore position
					restore_token_position(saved_pos);
				}
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		}
		
		// Parse pointer declarators: * [const] [volatile] *...
		while (peek() == "*"_tok) {
			advance(); // consume '*'
			
			// Parse cv-qualifiers after pointer
			CVQualifier ptr_cv = parse_cv_qualifiers();
			type_spec.add_pointer_level(ptr_cv);
		}
		
		// Check for function pointer/reference type syntax: ReturnType (&)(...) or ReturnType (*)(...) 
		// Pattern: Type (&)() = lvalue reference to function returning Type
		// Pattern: Type (&&)() = rvalue reference to function returning Type
		// Pattern: Type (*)() = pointer to function returning Type
		// This handles types like: int (&)(), _Xp (&)(), etc.
		if (peek() == "("_tok) {
			auto func_type_saved_pos = save_token_position();
			advance(); // consume '('
			
			// Check what's inside the parentheses: &, &&, or *
			bool is_function_ref = false;
			bool is_rvalue_function_ref = false;
			bool is_function_ptr = false;
			
			if (!peek().is_eof()) {
				if (peek() == "&&"_tok) {
					is_rvalue_function_ref = true;
					advance(); // consume '&&'
				} else if (peek() == "&"_tok) {
					is_function_ref = true;
					advance(); // consume '&'
					// Check for second & (in case lexer didn't combine them)
					if (peek() == "&"_tok) {
						is_rvalue_function_ref = true;
						is_function_ref = false;
						advance(); // consume second '&'
					}
				} else if (peek() == "*"_tok) {
					is_function_ptr = true;
					advance(); // consume '*'
				}
			}
			
			// After &, &&, or *, expect ')'
			if ((is_function_ref || is_rvalue_function_ref || is_function_ptr) &&
			    peek() == ")"_tok) {
				advance(); // consume ')'
				
				// Now expect '(' for the parameter list
				if (peek() == "("_tok) {
					advance(); // consume '('
					
					// Parse parameter list (can be empty or have parameters)
					// For now, we'll skip the parameter list - we just need to recognize the syntax
					// and accept it for type traits purposes
					std::vector<Type> param_types;
					while (!peek().is_eof() && peek() != ")"_tok) {
						// Skip parameter - can be complex types
						auto param_type_result = parse_type_specifier();
						if (!param_type_result.is_error() && param_type_result.node().has_value()) {
							const TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
							param_types.push_back(param_type.type());
						}
						
						// Handle pointer/reference/cv-qualifier modifiers after type
						while (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok ||
							   peek() == "const"_tok || peek() == "volatile"_tok) {
							advance();
						}
						
						// Handle pack expansion '...' (e.g., _Args...)
						if (peek() == "..."_tok) {
							advance(); // consume '...'
						}
						
						// Check for comma
						if (peek() == ","_tok) {
							advance(); // consume ','
						} else {
							break;
						}
					}
					
					if (peek() == ")"_tok) {
						advance(); // consume ')'
						
						// Successfully parsed function reference/pointer type!
						// Mark the type accordingly
						FunctionSignature func_sig;
						func_sig.return_type = type_spec.type();
						func_sig.parameter_types = std::move(param_types);
						
						if (is_function_ptr) {
							type_spec.add_pointer_level(CVQualifier::None);
						}
						type_spec.set_function_signature(func_sig);
						
						if (is_function_ref) {
							type_spec.set_reference_qualifier(ReferenceQualifier::LValueReference);  // lvalue reference
						} else if (is_rvalue_function_ref) {
							type_spec.set_reference_qualifier(ReferenceQualifier::RValueReference);   // rvalue reference
						}
						
						FLASH_LOG(Parser, Debug, "Parsed function reference/pointer type: ", 
						          is_function_ptr ? "pointer" : (is_rvalue_function_ref ? "rvalue ref" : "lvalue ref"),
						          " to function");
						
						// Discard saved position - we successfully parsed
						discard_saved_token(func_type_saved_pos);
					} else {
						// Parsing failed - restore position
						restore_token_position(func_type_saved_pos);
					}
				} else {
					// No parameter list follows - restore position
					restore_token_position(func_type_saved_pos);
				}
			} else if (!is_function_ref && !is_rvalue_function_ref && !is_function_ptr) {
				// Could be a bare function type: ReturnType(Args...)
				// e.g., using type = _Res(_Args...);
				// The '(' was already consumed, we're looking at the first parameter type or ')'
				std::vector<Type> param_types;
				bool parsed_bare_function_type = false;
				
				while (!peek().is_eof() && peek() != ")"_tok) {
					auto param_type_result = parse_type_specifier();
					if (param_type_result.is_error() || !param_type_result.node().has_value()) {
						break;
					}
					TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
					
					// Handle pointer/reference/cv-qualifier modifiers after type
					consume_pointer_ref_modifiers(param_type);
					
					// Handle pack expansion '...' (e.g., _Args...)
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						param_type.set_pack_expansion(true);
					}
					
					param_types.push_back(param_type.type());
					
					if (peek() == ","_tok) {
						advance(); // consume ','
					} else {
						break;
					}
				}
				
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					parsed_bare_function_type = true;
					
					FunctionSignature func_sig;
					func_sig.return_type = type_spec.type();
					func_sig.parameter_types = std::move(param_types);
					type_spec.set_function_signature(func_sig);
					
					FLASH_LOG(Parser, Debug, "Parsed bare function type in type alias");
					
					discard_saved_token(func_type_saved_pos);
				}
				
				if (!parsed_bare_function_type) {
					restore_token_position(func_type_saved_pos);
				}
			} else {
				// Not a function type syntax - restore position
				restore_token_position(func_type_saved_pos);
			}
		}
		
		// Parse reference modifiers: & or &&
		ReferenceQualifier ref_qual = parse_reference_qualifier();
		FLASH_LOG_FORMAT(Parser, Debug, "Type alias '{}': ref_qual={} (0=None, 1=LValue, 2=RValue)", 
			StringTable::getStringView(alias_name), static_cast<int>(ref_qual));
		type_spec.set_reference_qualifier(ref_qual);
		
		// Parse array dimensions: using _Type = _Tp[_Nm]; or using _Type = _Tp[2][3];
		while (peek() == "["_tok) {
			advance(); // consume '['
			if (peek() == "]"_tok) {
				type_spec.set_array(true);
				advance(); // consume ']'
			} else {
				auto dim_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (dim_result.is_error()) {
					return dim_result;
				}
				auto dim_val = try_evaluate_constant_expression(*dim_result.node());
				size_t dim_size = dim_val.has_value() ? static_cast<size_t>(dim_val->value) : 0;
				type_spec.add_array_dimension(dim_size);
				if (!consume("]"_tok)) {
					return ParseResult::error("Expected ']' after array dimension in type alias", current_token_);
				}
			}
		}
		
		// Consume semicolon
		if (!consume(";"_tok)) {
			return ParseResult::error("Expected ';' after type alias", current_token_);
		}
		
		// Store the alias in the struct (if struct_ref provided)
		if (struct_ref) {
			struct_ref->add_type_alias(alias_name, *type_result.node(), current_access);
		}
		
		// Also register it globally with qualified name (e.g., WithType::type)
		// (re-get type_spec since we modified it above)
		const TypeSpecifierNode& final_type_spec = type_result.node()->as<TypeSpecifierNode>();
		
		// Build qualified name if we're inside a struct
		StringHandle qualified_alias_name = alias_name;
		if (struct_ref) {
			StringBuilder qualified_builder;
			qualified_builder.append(struct_ref->name());
			qualified_builder.append("::");
			qualified_builder.append(alias_name);
			qualified_alias_name = StringTable::getOrInternStringHandle(qualified_builder.commit());
		}
		
		auto& alias_type_info = gTypeInfo.emplace_back(qualified_alias_name, final_type_spec.type(), final_type_spec.type_index(), final_type_spec.size_in_bits());
		alias_type_info.is_reference_ = final_type_spec.is_reference();
		alias_type_info.is_rvalue_reference_ = final_type_spec.is_rvalue_reference();
		alias_type_info.pointer_depth_ = final_type_spec.pointer_depth();
		gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
		
		return ParseResult::success();
	}
	
	// For 'typedef', check if this is an inline struct/enum definition
	// Pattern: typedef struct { ... } Alias;
	// Pattern: typedef enum { ... } Alias;
	if (!peek().is_eof() && 
	    (peek() == "struct"_tok || peek() == "class"_tok || peek() == "enum"_tok)) {
		// This is potentially an inline definition - use the full parse_typedef_declaration logic
		// We already consumed 'typedef', so we need to restore it
		// Actually, we can't restore easily, so let's handle it inline here
		
		bool is_enum = peek() == "enum"_tok;
		bool is_struct = peek() == "struct"_tok || peek() == "class"_tok;
		
		// Look ahead to check if it's really an inline definition
		auto saved_pos = save_token_position();
		advance(); // consume struct/class/enum
		
		bool is_inline_definition = false;
		if (!peek().is_eof()) {
			// If next token is '{', it's definitely inline: typedef struct { ... } Alias;
			if (peek() == "{"_tok) {
				is_inline_definition = true;
			} else if (peek().is_identifier()) {
				// Could be: typedef struct Name { ... } Alias; (inline)
				// or:       typedef struct Name Alias; (forward reference)
				advance(); // consume name
				if (!peek().is_eof() && (peek() == "{"_tok || peek() == ":"_tok)) {
					is_inline_definition = true;
				}
			}
		}
		
		restore_token_position(saved_pos);
		
		if (is_inline_definition && is_struct) {
			// Parse inline struct: typedef struct { ... } Alias; or typedef struct Name { ... } Alias;
			bool is_class = peek() == "class"_tok;
			advance(); // consume 'struct' or 'class'
			
			// Check if there's a struct name or if it's anonymous
			std::string_view struct_name_view;
			
			if (peek().is_identifier()) {
				struct_name_view = peek_info().value();
				advance(); // consume struct name
			} else {
				// Anonymous struct - generate a unique name using StringBuilder for persistent storage
				struct_name_view = StringBuilder()
					.append("__anonymous_typedef_struct_")
					.append(ast_nodes_.size())
					.commit();
			}
			
			// Register the struct type early
			StringHandle struct_name = StringTable::getOrInternStringHandle(struct_name_view);
			TypeInfo& struct_type_info = add_struct_type(struct_name);
			TypeIndex struct_type_index = struct_type_info.type_index_;
			// Create struct declaration node
			auto [struct_node, struct_ref_inner] = emplace_node_ref<StructDeclarationNode>(struct_name, is_class);
			
			// Create StructTypeInfo
			auto struct_info = std::make_unique<StructTypeInfo>(struct_name, is_class ? AccessSpecifier::Private : AccessSpecifier::Public);
			
			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' in struct definition", peek_info());
			}
			
			// Parse struct members (simplified - just type and name)
			AccessSpecifier member_access = struct_info->default_access;
			size_t member_count = 0;
			const size_t MAX_MEMBERS = 10000; // Safety limit
			
			while (!peek().is_eof() && peek() != "}"_tok && member_count < MAX_MEMBERS) {
				member_count++;
				
				// Parse member type
				auto member_type_result = parse_type_specifier();
				if (member_type_result.is_error()) {
					return member_type_result;
				}
				
				if (!member_type_result.node().has_value()) {
					return ParseResult::error("Expected type specifier in struct member", current_token_);
				}
				
				// Handle pointer declarators with CV-qualifiers (e.g., "unsigned short const* _locale_pctype")
				// Parse pointer declarators: * [const] [volatile] *...
				TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
				consume_pointer_ref_modifiers(member_type_spec);

				// Parse member name
				auto member_name_token = peek_info();
				if (!member_name_token.kind().is_identifier()) {
					FLASH_LOG(Parser, Debug, "Expected member name but got: type=",
						!member_name_token.kind().is_eof() ? static_cast<int>(member_name_token.type()) : -1,
						" value='", !member_name_token.kind().is_eof() ? member_name_token.value() : "NONE", "'");
					return ParseResult::error("Expected member name in struct", member_name_token);
				}
				advance(); // consume the member name

				std::optional<size_t> bitfield_width;
				
				// Handle bitfield declarations: unsigned int field:8;
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
					if (width_result.is_error()) {
						return width_result;
					}
					if (width_result.node().has_value()) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
						if (!eval_result.success() || eval_result.as_int() < 0) {
							return ParseResult::error("Bitfield width must be a non-negative integral constant expression", current_token_);
						}
						bitfield_width = static_cast<size_t>(eval_result.as_int());
					}
				}

				// Create member declaration
				auto member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token);
				struct_ref_inner.add_member(member_decl_node, member_access, std::nullopt, bitfield_width);
				
				// Handle comma-separated declarations
				while (peek() == ","_tok) {
					advance(); // consume ','
					auto next_name = advance();
					if (!next_name.kind().is_identifier()) {
						return ParseResult::error("Expected member name after comma", current_token_);
					}
					std::optional<size_t> next_bitfield_width;
					if (peek() == ":"_tok) {
						advance(); // consume ':'
						auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
						if (width_result.is_error()) {
							return width_result;
						}
						if (width_result.node().has_value()) {
							ConstExpr::EvaluationContext ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
							if (!eval_result.success() || eval_result.as_int() < 0) {
								return ParseResult::error("Bitfield width must be a non-negative integral constant expression", current_token_);
							}
							next_bitfield_width = static_cast<size_t>(eval_result.as_int());
						}
					}
					auto next_decl = emplace_node<DeclarationNode>(
						emplace_node<TypeSpecifierNode>(member_type_spec),
						next_name
					);
					struct_ref_inner.add_member(next_decl, member_access, std::nullopt, next_bitfield_width);
				}
				
				// Expect semicolon
				if (!consume(";"_tok)) {
					return ParseResult::error("Expected ';' after struct member", current_token_);
				}
			}
			
			if (member_count >= MAX_MEMBERS) {
				return ParseResult::error("Struct has too many members (possible infinite loop detected)", current_token_);
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after struct members", peek_info());
			}
			
			// Calculate struct layout
			for (const auto& member_decl : struct_ref_inner.members()) {
				const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
				const TypeSpecifierNode& member_type_spec = decl.type_node().as<TypeSpecifierNode>();
				
				// Calculate member size and alignment
				auto [member_size_in_bits, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
				
				// For struct types, get the actual size from TypeInfo
				if (member_type_spec.type() == Type::Struct) {
					TypeInfo* member_type_info = nullptr;
					for (auto& ti : gTypeInfo) {
						if (ti.type_index_ == member_type_spec.type_index()) {
							member_type_info = &ti;
							break;
						}
					}
					if (member_type_info && member_type_info->getStructInfo()) {
						member_size_in_bits = member_type_info->getStructInfo()->total_size;
						member_alignment = member_type_info->getStructInfo()->alignment;
					}
				}
				
				// Phase 7B: Intern member name and use StringHandle overload
				StringHandle member_name_handle = decl.identifier_token().handle();
				struct_info->addMember(
					member_name_handle,
					member_type_spec.type(),
					member_type_spec.type_index(),
					member_size_in_bits,
					member_alignment,
					member_access,
					std::nullopt,
					member_type_spec.is_reference(),
					member_type_spec.is_rvalue_reference(),
					member_type_spec.size_in_bits(),
					false,
					{},
					static_cast<int>(member_type_spec.pointer_depth()),
					member_decl.bitfield_width
				);
			}
			
			// Finalize struct layout
			if (!struct_info->finalize()) {
				return ParseResult::error(struct_info->getFinalizationError(), Token());
			}
			
			// Store struct info
			struct_type_info.setStructInfo(std::move(struct_info));
			// Update type_size_ from the finalized struct's total size
			if (struct_type_info.getStructInfo()) {
				struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
			}
			
			// Parse the typedef alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after struct definition", current_token_);
			}
			auto alias_name = alias_token.handle();
			
			// Consume semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after typedef", current_token_);
			}
			
			// Create type specifier for the typedef
			int struct_size_bits = 0;
			if (const StructTypeInfo* finalized_struct_info = struct_type_info.getStructInfo()) {
				struct_size_bits = static_cast<int>(finalized_struct_info->total_size * 8);
			}
			TypeSpecifierNode type_spec(
				Type::Struct,
				struct_type_index,
				struct_size_bits,
				alias_token
			);
			ASTNode type_node = emplace_node<TypeSpecifierNode>(type_spec);
			
			// Store the alias in the struct (if struct_ref provided)
			if (struct_ref) {
				struct_ref->add_type_alias(alias_name, type_node, current_access);
			}
			
			// Register the alias globally
			auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), gTypeInfo.size(), type_spec.size_in_bits());
			alias_type_info.type_index_ = type_spec.type_index();
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			return ParseResult::success();
		}
		
		if (is_inline_definition && is_enum) {
			// Parse inline enum: typedef enum { ... } Alias;
			advance(); // consume 'enum'
			
			// Check if there's an enum name or if it's anonymous
			StringHandle enum_name;
			
			if (peek().is_identifier()) {
				enum_name = peek_info().handle();
				advance(); // consume enum name
			} else {
				// Anonymous enum - generate a unique name using StringBuilder for persistent storage
				enum_name = StringTable::getOrInternStringHandle(StringBuilder()
					.append("__anonymous_typedef_enum_")
					.append(ast_nodes_.size()));
			}
			
			// Register the enum type early
			TypeInfo& enum_type_info = add_enum_type(enum_name);
			TypeIndex enum_type_index = enum_type_info.type_index_;
			
			// Create enum declaration node
			bool is_scoped = false;
			auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name, is_scoped);
			
			// Check for underlying type specification (: type)
			if (peek() == ":"_tok) {
				advance(); // consume ':'
				auto underlying_type_result = parse_type_specifier();
				if (underlying_type_result.is_error()) {
					return underlying_type_result;
				}
				if (auto underlying_type_node = underlying_type_result.node()) {
					enum_ref.set_underlying_type(*underlying_type_node);
				}
			}
			
			// Expect opening brace
			if (!consume("{"_tok)) {
				return ParseResult::error("Expected '{' in enum definition", peek_info());
			}
			
			// Create enum type info
			auto enum_info = std::make_unique<EnumTypeInfo>(enum_name, is_scoped);
			
			// Determine underlying type
			int underlying_size = 32;
			if (enum_ref.has_underlying_type()) {
				const auto& type_spec_node = enum_ref.underlying_type()->as<TypeSpecifierNode>();
				underlying_size = type_spec_node.size_in_bits();
			}
			
			// Parse enumerators
			int64_t next_value = 0;
			size_t enumerator_count = 0;
			const size_t MAX_ENUMERATORS = 10000; // Safety limit
			
			// Store enum info early so ConstExprEvaluator can look up values during parsing
			enum_type_info.setEnumInfo(std::move(enum_info));
			auto* live_enum_info = enum_type_info.getEnumInfo();
			
			// For scoped enums, push a temporary scope so that enumerator names
			// are visible to subsequent value expressions (C++ §9.7.1/2)
			if (is_scoped) {
				gSymbolTable.enter_scope(ScopeType::Block);
			}
			
			while (!peek().is_eof() && peek() != "}"_tok && enumerator_count < MAX_ENUMERATORS) {
				enumerator_count++;
				
				auto enumerator_name_token = advance();
				if (!enumerator_name_token.kind().is_identifier()) {
					if (is_scoped) gSymbolTable.exit_scope();
					return ParseResult::error("Expected enumerator name in enum", enumerator_name_token);
				}
				
				int64_t value = next_value;
				std::optional<ASTNode> enumerator_value;
				
				if (peek() == "="_tok) {
					advance(); // consume '='
					auto value_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
					if (value_expr_result.is_error()) {
						if (is_scoped) gSymbolTable.exit_scope();
						return value_expr_result;
					}
					if (auto value_node = value_expr_result.node()) {
						enumerator_value = *value_node;
						// Extract numeric value if possible
						bool value_extracted = false;
						if (value_node->is<ExpressionNode>()) {
							const auto& expr = value_node->as<ExpressionNode>();
							if (std::holds_alternative<NumericLiteralNode>(expr)) {
								const auto& lit = std::get<NumericLiteralNode>(expr);
								const auto& val = lit.value();
								if (std::holds_alternative<unsigned long long>(val)) {
									value = static_cast<int64_t>(std::get<unsigned long long>(val));
									value_extracted = true;
								}
							}
						}
						// Fallback: use ConstExprEvaluator for complex expressions
						if (!value_extracted) {
							ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
							auto eval_result = ConstExpr::Evaluator::evaluate(*value_node, eval_ctx);
							if (eval_result.success()) {
								value = eval_result.as_int();
							}
						}
					}
				}
				
				auto enumerator_node = emplace_node<EnumeratorNode>(enumerator_name_token, enumerator_value);
				enum_ref.add_enumerator(enumerator_node);
				// Phase 7B: Intern enumerator name and use StringHandle overload
				StringHandle enumerator_name_handle = enumerator_name_token.handle();
				live_enum_info->addEnumerator(enumerator_name_handle, value);
				
				// Add enumerator to current scope as DeclarationNode so codegen and
				// ConstExprEvaluator (via gTypeInfo enum lookup) can both find it
				{
					auto enum_type_node = emplace_node<TypeSpecifierNode>(
						Type::Enum, enum_type_index, underlying_size, enumerator_name_token);
					auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_name_token);
					gSymbolTable.insert(enumerator_name_token.value(), enumerator_decl);
				}
				
				next_value = value + 1;
				
				if (peek() == ","_tok) {
					advance();
					if (peek() == "}"_tok) {
						break;
					}
				} else {
					break;
				}
			}
			
			if (enumerator_count >= MAX_ENUMERATORS) {
				if (is_scoped) gSymbolTable.exit_scope();
				return ParseResult::error("Enum has too many enumerators (possible infinite loop detected)", current_token_);
			}
			
			// Pop temporary scope for scoped enums
			if (is_scoped) {
				gSymbolTable.exit_scope();
			}
			
			// Expect closing brace
			if (!consume("}"_tok)) {
				return ParseResult::error("Expected '}' after enum enumerators", peek_info());
			}
			
			// enum_info was already stored in gTypeInfo before the loop
			
			// Parse the typedef alias name
			auto alias_token = advance();
			if (!alias_token.kind().is_identifier()) {
				return ParseResult::error("Expected alias name after enum definition", current_token_);
			}
			auto alias_name = alias_token.handle();
			
			// Consume semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after typedef", current_token_);
			}
			
			// Create type specifier for the typedef
			TypeSpecifierNode type_spec(Type::Enum, TypeQualifier::None, underlying_size, alias_token);
			type_spec.set_type_index(enum_type_index);
			ASTNode type_node = emplace_node<TypeSpecifierNode>(type_spec);
			
			// Store the alias in the struct (if struct_ref provided)
			if (struct_ref) {
				struct_ref->add_type_alias(alias_name, type_node, current_access);
			}
			
			// Register the alias globally
			auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
			gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
			
			return ParseResult::success();
		}
	}
	
	// Simple typedef: typedef Type Alias;
	// Parse the type
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}
	
	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type after 'typedef'", current_token_);
	}
	
	ASTNode type_node = *type_result.node();
	TypeSpecifierNode type_spec = type_node.as<TypeSpecifierNode>();
	consume_pointer_ref_modifiers(type_spec);

	// Check for pointer-to-member type syntax: typedef Type Class::* alias;
	// This is used in <type_traits> for result_of patterns
	// Pattern: typedef _Res _Class::* _MemPtr;
	if (peek().is_identifier()) {
		// Look ahead to see if this is Class::* pattern
		SaveHandle saved_pos = save_token_position();
		Token class_token = peek_info();
		advance(); // consume potential class name
		
		if (peek() == "::"_tok) {
			advance(); // consume '::'
			if (peek() == "*"_tok) {
				advance(); // consume '*'
				// This is a pointer-to-member type: Type Class::*
				// Mark the type as a pointer-to-member
				type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
				type_spec.set_member_class_name(class_token.handle());
				FLASH_LOG(Parser, Debug, "Parsed pointer-to-member typedef in member_type_alias: ", type_spec.token().value(), " ", class_token.value(), "::*");
				discard_saved_token(saved_pos);
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		} else {
			// Not a pointer-to-member, restore position
			restore_token_position(saved_pos);
		}
	}
	
	// Check for function pointer typedef: typedef ReturnType (*Name)(Params);
	// Pattern: typedef void (*event_callback)(event e, ios_base& b, int i);
	if (peek() == "("_tok) {
		SaveHandle fnptr_check = save_token_position();
		advance(); // consume '('
		if (peek() == "*"_tok) {
			advance(); // consume '*'
			if (peek().is_identifier()) {
				Token fnptr_name_token = peek_info();
				advance(); // consume alias name
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					// Skip the parameter list
					if (peek() == "("_tok) {
						skip_balanced_parens();
					}
					discard_saved_token(fnptr_check);

					auto alias_name = fnptr_name_token.handle();

					// Register as a function pointer type (treat as void* for now)
					type_spec.add_pointer_level(CVQualifier::None);
					type_node = emplace_node<TypeSpecifierNode>(type_spec);

					// Store the alias in the struct (if struct_ref provided)
					if (struct_ref) {
						struct_ref->add_type_alias(alias_name, type_node, current_access);
					}

					// Register the alias globally
					auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
					gTypesByName.emplace(alias_type_info.name(), &alias_type_info);

					// Consume semicolon
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after typedef", current_token_);
					}

					return ParseResult::success();
				}
			}
		}
		restore_token_position(fnptr_check);
	}

	// Parse the typedef alias name
	auto alias_token = peek_info();
	if (!alias_token.kind().is_identifier()) {
		return ParseResult::error("Expected alias name in typedef", peek_info());
	}
	
	auto alias_name = alias_token.handle();
	advance(); // consume alias name
	
	// Skip C++11 attributes that may follow the alias name (e.g., typedef T name [[__deprecated__]];)
	// This is a GNU extension where attributes can appear on the declarator in a typedef
	skip_cpp_attributes();
	
	// Consume semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after typedef", current_token_);
	}
	
	// Update type_node with modified type_spec (with pointers)
	type_node = emplace_node<TypeSpecifierNode>(type_spec);
	
	// Store the alias in the struct (if struct_ref provided)
	if (struct_ref) {
		struct_ref->add_type_alias(alias_name, type_node, current_access);
	}
	
	// Also register it globally
	auto& alias_type_info = gTypeInfo.emplace_back(alias_name, type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
	alias_type_info.is_rvalue_reference_ = type_spec.is_rvalue_reference();
	gTypesByName.emplace(alias_type_info.name(), &alias_type_info);
	
	return ParseResult::success();
}

ParseResult Parser::parse_typedef_declaration()
{
	ScopedTokenPosition saved_position(*this);

	// Consume 'typedef' keyword
	auto typedef_keyword = advance();
	if (typedef_keyword.kind() != "typedef"_tok) {
		return ParseResult::error("Expected 'typedef' keyword", typedef_keyword);
	}

	// Check if this is an inline struct/class definition: typedef struct { ... } alias;
	// or typedef struct Name { ... } alias;
	bool is_inline_struct = false;
	StringHandle struct_name_for_typedef;
	TypeIndex struct_type_index = 0;

	// Check if this is an inline enum definition: typedef enum { ... } alias;
	// or typedef enum _Name { ... } alias;
	bool is_inline_enum = false;
	StringHandle enum_name_for_typedef;
	TypeIndex enum_type_index = 0;

	if (peek() == "enum"_tok) {
		// Look ahead to see if this is an inline definition
		// Pattern 1: typedef enum { ... } alias;
		// Pattern 2: typedef enum _Name { ... } alias;
		// Pattern 3: typedef enum class Name { ... } alias;
		auto next_pos = current_token_;
		advance(); // consume 'enum'

		// Check for 'class' or 'struct' keyword (enum class / enum struct)
		[[maybe_unused]] bool has_class_keyword = false;
		if (peek().is_keyword() &&
		    (peek() == "class"_tok || peek() == "struct"_tok)) {
			has_class_keyword = true;
			advance(); // consume 'class' or 'struct'
		}

		// Check if next token is '{' (anonymous enum) or identifier followed by ':' or '{'
		if (peek() == "{"_tok) {
			// Pattern 1: typedef enum { ... } alias;
			is_inline_enum = true;
			enum_name_for_typedef = StringTable::getOrInternStringHandle(StringBuilder().append("__anonymous_typedef_enum_"sv).append(ast_nodes_.size()));
		} else if (peek().is_identifier()) {
			auto enum_name_token = peek_info();
			advance(); // consume enum name

			if (!peek().is_eof() && 
			    (peek() == "{"_tok || peek() == ":"_tok)) {
				// Pattern 2: typedef enum _Name { ... } alias;
				// or typedef enum _Name : type { ... } alias;
				is_inline_enum = true;
				enum_name_for_typedef = enum_name_token.handle();
			} else {
				// Not an inline definition, restore position and parse normally
				current_token_ = next_pos;
				is_inline_enum = false;
			}
		} else {
			// Not an inline definition, restore position and parse normally
			current_token_ = next_pos;
			is_inline_enum = false;
		}
	} else if (!peek().is_eof() &&
	    (peek() == "struct"_tok || peek() == "class"_tok || peek() == "union"_tok)) {
		// Look ahead to see if this is an inline definition
		// Pattern 1: typedef struct { ... } alias;
		// Pattern 2: typedef struct Name { ... } alias;
		// Pattern 3: typedef union { ... } alias;
		// Pattern 4: typedef union Name { ... } alias;
		SaveHandle next_pos = save_token_position();
		advance(); // consume 'struct', 'class', or 'union'

		// Check if next token is '{' (anonymous struct/union) or identifier followed by '{'
		if (peek() == "{"_tok) {
			// Pattern 1/3: typedef struct/union { ... } alias;
			is_inline_struct = true;
			// Use a unique temporary name for the struct/union (will be replaced by typedef alias)
			// Use the current AST size to make it unique
			struct_name_for_typedef = StringTable::getOrInternStringHandle(StringBuilder().append("__anonymous_typedef_struct_"sv).append(ast_nodes_.size()));
			discard_saved_token(next_pos);
		} else if (peek().is_identifier()) {
			auto struct_name_token = peek_info();
			advance(); // consume struct/union name

			if (peek() == "{"_tok) {
				// Pattern 2/4: typedef struct/union Name { ... } alias;
				is_inline_struct = true;
				struct_name_for_typedef = struct_name_token.handle();
				discard_saved_token(next_pos);
			} else {
				// Not an inline definition, restore position and parse normally
				restore_token_position(next_pos);
				is_inline_struct = false;
			}
		} else {
			// Not an inline definition, restore position and parse normally
			restore_token_position(next_pos);
			is_inline_struct = false;
		}
	}

	ASTNode type_node;
	TypeSpecifierNode type_spec;

	if (is_inline_enum) {
		// Parse the inline enum definition
		// We need to manually parse the enum body since we already consumed the keyword and name

		// Register the enum type early
		TypeInfo& enum_type_info = add_enum_type(enum_name_for_typedef);
		enum_type_index = enum_type_info.type_index_;

		// Create enum declaration node
		// Note: We don't know if it's scoped yet - we'll determine from the parsing context
		bool is_scoped = false; // C-style typedef enum is typically not scoped
		auto [enum_node, enum_ref] = emplace_node_ref<EnumDeclarationNode>(enum_name_for_typedef, is_scoped);

		// Check for underlying type specification (: type)
		if (peek() == ":"_tok) {
			advance(); // consume ':'

			// Parse the underlying type
			auto underlying_type_result = parse_type_specifier();
			if (underlying_type_result.is_error()) {
				return underlying_type_result;
			}

			if (auto underlying_type_node = underlying_type_result.node()) {
				enum_ref.set_underlying_type(*underlying_type_node);
			}
		}

		// Expect opening brace
		if (!consume("{"_tok)) {
			return ParseResult::error("Expected '{' in enum definition", peek_info());
		}

		// Create enum type info
		auto enum_info = std::make_unique<EnumTypeInfo>(enum_name_for_typedef, is_scoped);

		// Determine underlying type (default is int)
		int underlying_size = 32;
		if (enum_ref.has_underlying_type()) {
			const auto& type_spec_node = enum_ref.underlying_type()->as<TypeSpecifierNode>();
			underlying_size = type_spec_node.size_in_bits();
		}

		// Store enum info early so ConstExprEvaluator can look up values during parsing
		auto& enum_type_info_ref = gTypeInfo[enum_type_index];
		enum_type_info_ref.setEnumInfo(std::move(enum_info));
		auto* live_enum_info = enum_type_info_ref.getEnumInfo();

		// Parse enumerators
		int64_t next_value = 0;
		// For scoped enums, push a temporary scope so that enumerator names
		// are visible to subsequent value expressions (C++ §9.7.1/2)
		if (is_scoped) {
			gSymbolTable.enter_scope(ScopeType::Block);
		}
		while (!peek().is_eof() && peek() != "}"_tok) {
			// Parse enumerator name
			auto enumerator_name_token = advance();
			if (!enumerator_name_token.kind().is_identifier()) {
				if (is_scoped) gSymbolTable.exit_scope();
				return ParseResult::error("Expected enumerator name in enum", enumerator_name_token);
			}

			int64_t value = next_value;
			std::optional<ASTNode> enumerator_value;

			// Check for explicit value
			if (peek() == "="_tok) {
				advance(); // consume '='

				// Parse constant expression
				auto value_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (value_expr_result.is_error()) {
					if (is_scoped) gSymbolTable.exit_scope();
					return value_expr_result;
				}

				// Extract value from expression
				if (auto value_node = value_expr_result.node()) {
					enumerator_value = *value_node;
					
					bool value_extracted = false;
					if (value_node->is<ExpressionNode>()) {
						const auto& expr = value_node->as<ExpressionNode>();
						if (std::holds_alternative<NumericLiteralNode>(expr)) {
							const auto& lit = std::get<NumericLiteralNode>(expr);
							const auto& val = lit.value();
							if (std::holds_alternative<unsigned long long>(val)) {
								value = static_cast<int64_t>(std::get<unsigned long long>(val));
								value_extracted = true;
							} else if (std::holds_alternative<double>(val)) {
								value = static_cast<int64_t>(std::get<double>(val));
								value_extracted = true;
							}
						}
					}
					// Fallback: use ConstExprEvaluator for complex expressions
					if (!value_extracted) {
						ConstExpr::EvaluationContext eval_ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*value_node, eval_ctx);
						if (eval_result.success()) {
							value = eval_result.as_int();
						}
					}
				}
			}

			// Add enumerator
			auto enumerator_node = emplace_node<EnumeratorNode>(enumerator_name_token, enumerator_value);
			enum_ref.add_enumerator(enumerator_node);
			// Phase 7B: Intern enumerator name and use StringHandle overload
			StringHandle enumerator_name_handle = enumerator_name_token.handle();
			live_enum_info->addEnumerator(enumerator_name_handle, value);

			// Add enumerator to current scope as DeclarationNode so codegen and
			// ConstExprEvaluator (via gTypeInfo enum lookup) can both find it
			{
				auto enum_type_node = emplace_node<TypeSpecifierNode>(
					Type::Enum, enum_type_index, underlying_size, enumerator_name_token);
				auto enumerator_decl = emplace_node<DeclarationNode>(enum_type_node, enumerator_name_token);
				gSymbolTable.insert(enumerator_name_token.value(), enumerator_decl);
			}

			next_value = value + 1;

			// Check for comma (more enumerators) or closing brace
			if (peek() == ","_tok) {
				advance(); // consume ','
				// Allow trailing comma before '}'
				if (peek() == "}"_tok) {
					break;
				}
			} else {
				break;
			}
		}

		// Pop temporary scope for scoped enums
		if (is_scoped) {
			gSymbolTable.exit_scope();
		}

		// Expect closing brace
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after enum enumerators", peek_info());
		}

		// enum_info was already stored in gTypeInfo before the loop

		// Add enum declaration to AST
		gSymbolTable.insert(enum_name_for_typedef, enum_node);
		ast_nodes_.push_back(enum_node);

		// Create type specifier for the typedef
		type_spec = TypeSpecifierNode(Type::Enum, TypeQualifier::None, underlying_size, typedef_keyword);
		type_spec.set_type_index(enum_type_index);
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else if (is_inline_struct) {
		// Parse the inline struct definition
		// We need to manually parse the struct body since we already consumed the keyword and name

		// Register the struct type early
		TypeInfo& struct_type_info = add_struct_type(struct_name_for_typedef);
		struct_type_index = struct_type_info.type_index_;

		// Create struct declaration node
		auto [struct_node, struct_ref] = emplace_node_ref<StructDeclarationNode>(struct_name_for_typedef, false);

		// Push struct parsing context
		struct_parsing_context_stack_.push_back({
			StringTable::getStringView(struct_name_for_typedef),
			&struct_ref,
			nullptr,
			gSymbolTable.get_current_namespace_handle(),
			{}
		});

		// Create StructTypeInfo
		auto struct_info = std::make_unique<StructTypeInfo>(struct_name_for_typedef, AccessSpecifier::Public);
		
		// Update the struct parsing context with the local_struct_info for static member lookup
		if (!struct_parsing_context_stack_.empty()) {
			struct_parsing_context_stack_.back().local_struct_info = struct_info.get();
		}

		// Apply pack alignment from #pragma pack
		size_t pack_alignment = context_.getCurrentPackAlignment();
		if (pack_alignment > 0) {
			struct_info->set_pack_alignment(pack_alignment);
		}

		// Expect opening brace
		if (!consume("{"_tok)) {
			return ParseResult::error("Expected '{' in struct definition", peek_info());
		}

		// Parse struct members (simplified version - no inheritance, no member functions for now)
		std::vector<StructMemberDecl> members;
		AccessSpecifier current_access = AccessSpecifier::Public;

		while (!peek().is_eof() && peek() != "}"_tok) {
			// Check for anonymous union/struct (union { ... };)
			if (peek().is_keyword() &&
			    (peek() == "union"_tok || peek() == "struct"_tok)) {
				// Peek ahead to see if this is anonymous (followed by '{')
				SaveHandle saved_pos = save_token_position();
				auto union_or_struct_keyword = advance(); // consume 'union' or 'struct'
				bool is_union = (union_or_struct_keyword.value() == "union");
				
				if (peek() == "{"_tok) {
					// Could be true anonymous union (union { ... };) or named anonymous (union { ... } name;)
					// Peek ahead to determine which pattern
					SaveHandle brace_start_pos = save_token_position();
					skip_balanced_braces();
					bool is_named_anonymous = false;
					if (peek().is_identifier()) {
						is_named_anonymous = true;
					}
					// Restore position to the opening brace to parse the members
					restore_token_position(brace_start_pos);
					
					// Now consume the opening brace
					advance(); // consume '{'
					
					if (is_named_anonymous) {
						// Named anonymous union/struct: union { ... } member_name;
						// Create an anonymous type and parse members into it
						
						// Generate a unique name for the anonymous union/struct type
						static int typedef_anonymous_type_counter = 0;
						std::string_view anon_type_name = StringBuilder()
							.append("__typedef_anonymous_")
							.append(is_union ? "union_" : "struct_")
							.append(static_cast<int64_t>(typedef_anonymous_type_counter++))
							.commit();
						StringHandle anon_type_name_handle = StringTable::getOrInternStringHandle(anon_type_name);
						
						// Create the anonymous struct/union type
						TypeInfo& anon_type_info = add_struct_type(anon_type_name_handle);
						
						// Create StructTypeInfo
						auto anon_struct_info_ptr = std::make_unique<StructTypeInfo>(anon_type_name_handle, AccessSpecifier::Public);
						StructTypeInfo* anon_struct_info = anon_struct_info_ptr.get();
						
						// Set the union flag if this is a union
						if (is_union) {
							anon_struct_info->is_union = true;
						}
						
						// Parse all members using the recursive helper
						ParseResult members_result = parse_anonymous_struct_union_members(anon_struct_info, anon_type_name);
						if (members_result.is_error()) {
							return members_result;
						}
						
						// Expect closing brace
						if (!consume("}"_tok)) {
							return ParseResult::error("Expected '}' after named anonymous union/struct members in typedef", peek_info());
						}
						
						// Calculate the layout for the anonymous type
						if (is_union) {
							// Union layout: all members at offset 0, size is max of all member sizes
							size_t max_size = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								member.offset = 0;  // All union members at offset 0
								if (member.size > max_size) {
									max_size = member.size;
								}
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							anon_struct_info->total_size = max_size;
							anon_struct_info->alignment = max_alignment;
						} else {
							// Struct layout: sequential members with alignment
							size_t current_offset = 0;
							size_t max_alignment = 1;
							for (auto& member : anon_struct_info->members) {
								// Align current offset
								if (member.alignment > 0) {
									current_offset = (current_offset + member.alignment - 1) & ~(member.alignment - 1);
								}
								member.offset = current_offset;
								current_offset += member.size;
								if (member.alignment > max_alignment) {
									max_alignment = member.alignment;
								}
							}
							// Final alignment padding
							if (max_alignment > 0) {
								current_offset = (current_offset + max_alignment - 1) & ~(max_alignment - 1);
							}
							anon_struct_info->total_size = current_offset;
							anon_struct_info->alignment = max_alignment;
						}
						
						// Set the struct info on the type info
						anon_type_info.setStructInfo(std::move(anon_struct_info_ptr));
						
						// Now parse the member name(s) - handle comma-separated declarators
						do {
							// Parse declarator name and pointer levels
							int ptr_levels = 0;
							while (peek() == "*"_tok) {
								advance(); // consume '*'
								ptr_levels++;
							}
							
							auto member_name_token = peek_info();
							if (!member_name_token.kind().is_identifier()) {
								return ParseResult::error("Expected member name after named anonymous union/struct in typedef", member_name_token);
							}
							advance(); // consume the member name
							
							// Create type specifier for the anonymous type
							TypeSpecifierNode anon_type_spec(Type::Struct, TypeQualifier::None, 
								static_cast<int>(anon_type_info.getStructInfo()->total_size * 8), union_or_struct_keyword);
							anon_type_spec.set_type_index(anon_type_info.type_index_);
							for (int i = 0; i < ptr_levels; i++) {
								anon_type_spec.add_pointer_level(CVQualifier::None);
							}
							
							// Create declaration node
							ASTNode type_node_for_member = emplace_node<TypeSpecifierNode>(anon_type_spec);
							ASTNode member_decl_node = emplace_node<DeclarationNode>(type_node_for_member, member_name_token);
							
							// Add as member of enclosing struct
							members.push_back({member_decl_node, current_access, std::nullopt});
							struct_ref.add_member(member_decl_node, current_access, std::nullopt);
							
						} while (peek() == ","_tok && (advance(), true));
						
						// Expect semicolon after the member declarations
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after named anonymous union/struct member in typedef", current_token_);
						}
						
						discard_saved_token(saved_pos);
						discard_saved_token(brace_start_pos);
						continue;  // Skip to next member
					}
					
					// True anonymous union/struct - parse and flatten members (original code path)
					// Parse all members of the anonymous union
					std::vector<StructMemberDecl> anon_members;
					while (!peek().is_eof() && peek() != "}"_tok) {
						// Check for nested anonymous union
						if (peek().is_keyword() &&
						    (peek() == "union"_tok || peek() == "struct"_tok)) {
							SaveHandle nested_saved_pos = save_token_position();
							advance(); // consume 'union' or 'struct'
							
							if (peek() == "{"_tok) {
								// Nested anonymous union - parse recursively
								advance(); // consume '{'
								
								// Parse nested anonymous union members
								while (!peek().is_eof() && peek() != "}"_tok) {
									// Parse member type
									auto nested_member_type_result = parse_type_specifier();
									if (nested_member_type_result.is_error()) {
										return nested_member_type_result;
									}
									
									if (!nested_member_type_result.node().has_value()) {
										return ParseResult::error("Expected type specifier in nested anonymous union", current_token_);
									}
									
									// Handle pointer declarators
									TypeSpecifierNode& nested_member_type_spec = nested_member_type_result.node()->as<TypeSpecifierNode>();
									while (peek() == "*"_tok) {
										advance(); // consume '*'
										CVQualifier ptr_cv = parse_cv_qualifiers();
										nested_member_type_spec.add_pointer_level(ptr_cv);
									}
									
									// Parse member name
									auto nested_member_name_token = peek_info();
									if (!nested_member_name_token.kind().is_identifier()) {
										return ParseResult::error("Expected member name in nested anonymous union", nested_member_name_token);
									}
									advance(); // consume the member name
									
									// Check for array declarator
									std::vector<ASTNode> nested_array_dimensions;
									while (peek() == "["_tok) {
										advance(); // consume '['
										
										// Parse the array size expression
										ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
										if (size_result.is_error()) {
											return size_result;
										}
										nested_array_dimensions.push_back(*size_result.node());
										
										// Expect closing ']'
										if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
										    peek() != "]"_tok) {
											return ParseResult::error("Expected ']' after array size", current_token_);
										}
										advance(); // consume ']'
									}
									
									// Create member declaration for nested union member
									ASTNode nested_member_decl_node;
									if (!nested_array_dimensions.empty()) {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token, std::move(nested_array_dimensions));
									} else {
										nested_member_decl_node = emplace_node<DeclarationNode>(*nested_member_type_result.node(), nested_member_name_token);
									}
									// Flatten nested union members into outer union
									anon_members.push_back({nested_member_decl_node, current_access, std::nullopt});
									
									// Expect semicolon
									if (!consume(";"_tok)) {
										return ParseResult::error("Expected ';' after nested anonymous union member", current_token_);
									}
								}
								
								// Expect closing brace for nested union
								if (!consume("}"_tok)) {
									return ParseResult::error("Expected '}' after nested anonymous union members", peek_info());
								}
								
								// Expect semicolon after nested anonymous union
								if (!consume(";"_tok)) {
									return ParseResult::error("Expected ';' after nested anonymous union", current_token_);
								}
								
								discard_saved_token(nested_saved_pos);
								continue; // Continue with next member of outer union
							} else {
								// Named union/struct - restore position and parse normally
								restore_token_position(nested_saved_pos);
							}
						}
						
						// Parse member type
						auto anon_member_type_result = parse_type_specifier();
						if (anon_member_type_result.is_error()) {
							return anon_member_type_result;
						}
						
						if (!anon_member_type_result.node().has_value()) {
							return ParseResult::error("Expected type specifier in anonymous union", current_token_);
						}
						
						// Handle pointer declarators
						TypeSpecifierNode& anon_member_type_spec = anon_member_type_result.node()->as<TypeSpecifierNode>();
						while (peek() == "*"_tok) {
							advance(); // consume '*'
							CVQualifier ptr_cv = parse_cv_qualifiers();
							anon_member_type_spec.add_pointer_level(ptr_cv);
						}
						
						// Parse member name
						auto anon_member_name_token = peek_info();
						if (!anon_member_name_token.kind().is_identifier()) {
							return ParseResult::error("Expected member name in anonymous union", anon_member_name_token);
						}
						advance(); // consume the member name
						
						// Check for array declarator
						std::vector<ASTNode> anon_array_dimensions;
						while (peek() == "["_tok) {
							advance(); // consume '['
							
							// Parse the array size expression
							ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
							if (size_result.is_error()) {
								return size_result;
							}
							anon_array_dimensions.push_back(*size_result.node());
							
							// Expect closing ']'
							if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
							    peek() != "]"_tok) {
								return ParseResult::error("Expected ']' after array size", current_token_);
							}
							advance(); // consume ']'
						}
						
						// Create member declaration
						ASTNode anon_member_decl_node;
						if (!anon_array_dimensions.empty()) {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token, std::move(anon_array_dimensions));
						} else {
							anon_member_decl_node = emplace_node<DeclarationNode>(*anon_member_type_result.node(), anon_member_name_token);
						}
						anon_members.push_back({anon_member_decl_node, current_access, std::nullopt});
						
						// Expect semicolon
						if (!consume(";"_tok)) {
							return ParseResult::error("Expected ';' after anonymous union member", current_token_);
						}
					}
					
					// Expect closing brace
					if (!consume("}"_tok)) {
						return ParseResult::error("Expected '}' after anonymous union members", peek_info());
					}
					
					// Expect semicolon after anonymous union
					if (!consume(";"_tok)) {
						return ParseResult::error("Expected ';' after anonymous union", current_token_);
					}
					
					// Flatten anonymous union members into parent struct
					// All members of an anonymous union share the same offset
					for (const auto& anon_member : anon_members) {
						members.push_back(anon_member);
						struct_ref.add_member(anon_member.declaration, anon_member.access, anon_member.default_initializer);
					}
					
					discard_saved_token(saved_pos);
					continue;
				} else {
					// Named union/struct - restore and parse as type
					restore_token_position(saved_pos);
				}
			}
			
			// Parse member declaration
			auto member_type_result = parse_type_specifier();
			if (member_type_result.is_error()) {
				return member_type_result;
			}

			if (!member_type_result.node().has_value()) {
				return ParseResult::error("Expected type specifier in struct member", current_token_);
			}

			// Handle pointer declarators with CV-qualifiers (e.g., "unsigned short const* _locale_pctype")
			// Parse pointer declarators: * [const] [volatile] *...
			TypeSpecifierNode& member_type_spec = member_type_result.node()->as<TypeSpecifierNode>();
			while (peek() == "*"_tok) {
				advance(); // consume '*'

				// Check for CV-qualifiers after the *
				CVQualifier ptr_cv = parse_cv_qualifiers();

				// Add pointer level to the type specifier
				member_type_spec.add_pointer_level(ptr_cv);
			}

			// Parse member name
			auto member_name_token = peek_info();
			if (!member_name_token.kind().is_identifier()) {
				return ParseResult::error("Expected member name in struct", member_name_token);
			}
			advance(); // consume the member name

			// Check for array declarator: '[' size ']' or multidimensional '[' size1 '][' size2 ']'...
			std::vector<ASTNode> array_dimensions;
			while (peek() == "["_tok) {
				advance(); // consume '['

				// Parse the array size expression
				ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
				if (size_result.is_error()) {
					return size_result;
				}
				array_dimensions.push_back(*size_result.node());

				// Expect closing ']'
				if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
				    peek() != "]"_tok) {
					return ParseResult::error("Expected ']' after array size", current_token_);
				}
				advance(); // consume ']'
			}

			std::optional<size_t> bitfield_width;
			std::optional<ASTNode> bitfield_width_expr;
			// Handle bitfield declarations: unsigned int field:8;
			if (peek() == ":"_tok) {
				advance(); // consume ':'
				auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
				if (width_result.is_error()) {
					return width_result;
				}
				if (width_result.node().has_value()) {
					ConstExpr::EvaluationContext ctx(gSymbolTable);
					auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
					if (!eval_result.success() || eval_result.as_int() < 0) {
						// Defer evaluation for template non-type parameters
						bitfield_width_expr = *width_result.node();
					} else {
						bitfield_width = static_cast<size_t>(eval_result.as_int());
					}
				}
			}

			// Create member declaration
			ASTNode member_decl_node;
			if (!array_dimensions.empty()) {
				member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token, std::move(array_dimensions));
			} else {
				member_decl_node = emplace_node<DeclarationNode>(*member_type_result.node(), member_name_token);
			}
			members.push_back({member_decl_node, current_access, std::nullopt, bitfield_width});
			members.back().bitfield_width_expr = bitfield_width_expr;
			struct_ref.add_member(member_decl_node, current_access, std::nullopt, bitfield_width, bitfield_width_expr);

			// Handle comma-separated declarations (e.g., int x, y, z;)
			while (peek() == ","_tok) {
				advance(); // consume ','

				// Parse the next member name
				auto next_member_name = advance();
				if (!next_member_name.kind().is_identifier()) {
					return ParseResult::error("Expected member name after comma", current_token_);
				}

				std::optional<size_t> additional_bitfield_width;
				std::optional<ASTNode> additional_bitfield_width_expr;
				if (peek() == ":"_tok) {
					advance(); // consume ':'
					auto width_result = parse_expression(4, ExpressionContext::Normal); // Precedence 4: stop before assignment (=) for default member initializers
					if (width_result.is_error()) {
						return width_result;
					}
					if (width_result.node().has_value()) {
						ConstExpr::EvaluationContext ctx(gSymbolTable);
						auto eval_result = ConstExpr::Evaluator::evaluate(*width_result.node(), ctx);
						if (!eval_result.success() || eval_result.as_int() < 0) {
							// Defer evaluation for template non-type parameters
							additional_bitfield_width_expr = *width_result.node();
						} else {
							additional_bitfield_width = static_cast<size_t>(eval_result.as_int());
						}
					}
				}

				// Create declaration with same type
				auto next_member_decl = emplace_node<DeclarationNode>(
					emplace_node<TypeSpecifierNode>(member_type_spec),
					next_member_name
				);
				members.push_back({next_member_decl, current_access, std::nullopt, additional_bitfield_width});
				members.back().bitfield_width_expr = additional_bitfield_width_expr;
				struct_ref.add_member(next_member_decl, current_access, std::nullopt, additional_bitfield_width, additional_bitfield_width_expr);
			}

			// Expect semicolon
			if (!consume(";"_tok)) {
				return ParseResult::error("Expected ';' after struct member", current_token_);
			}
		}

		// Expect closing brace
		if (!consume("}"_tok)) {
			return ParseResult::error("Expected '}' after struct members", peek_info());
		}

		// Pop struct parsing context
		struct_parsing_context_stack_.pop_back();

		// Calculate struct layout
		for (const auto& member_decl : members) {
			const DeclarationNode& decl = member_decl.declaration.as<DeclarationNode>();
			const TypeSpecifierNode& member_type_spec = decl.type_node().as<TypeSpecifierNode>();

			// Calculate member size and alignment
			auto [member_size, member_alignment] = calculateMemberSizeAndAlignment(member_type_spec);
			size_t referenced_size_bits = member_type_spec.size_in_bits();

			if (member_type_spec.type() == Type::Struct) {
				const TypeInfo* member_type_info = nullptr;
				for (const auto& ti : gTypeInfo) {
					if (ti.type_index_ == member_type_spec.type_index()) {
						member_type_info = &ti;
						break;
					}
				}

				if (member_type_info && member_type_info->getStructInfo()) {
					member_size = member_type_info->getStructInfo()->total_size;
					referenced_size_bits = static_cast<size_t>(member_type_info->getStructInfo()->total_size * 8);
					member_alignment = member_type_info->getStructInfo()->alignment;
				}
			}

			bool is_ref_member = member_type_spec.is_reference();
			bool is_rvalue_ref_member = member_type_spec.is_rvalue_reference();
			if (is_ref_member) {
				// Size and alignment were already set correctly above for references
				referenced_size_bits = referenced_size_bits ? referenced_size_bits : member_type_spec.size_in_bits();
			}
			// Phase 7B: Intern member name and use StringHandle overload
			StringHandle member_name_handle = decl.identifier_token().handle();
			struct_info->addMember(
				member_name_handle,
				member_type_spec.type(),
				member_type_spec.type_index(),
				member_size,
				member_alignment,
				member_decl.access,
				member_decl.default_initializer,
				is_ref_member,
				is_rvalue_ref_member,
				referenced_size_bits,
				false,
				{},
				static_cast<int>(member_type_spec.pointer_depth()),
				member_decl.bitfield_width
			);
		}

		// Finalize struct layout (add padding)
		if (!struct_info->finalize()) {
			return ParseResult::error(struct_info->getFinalizationError(), Token());
		}

		// Store struct info
		struct_type_info.setStructInfo(std::move(struct_info));
		// Update type_size_ from the finalized struct's total size
		if (struct_type_info.getStructInfo()) {
			struct_type_info.type_size_ = struct_type_info.getStructInfo()->total_size;
		}

		// Create type specifier for the struct
		// Note: Use struct_type_info.getStructInfo() since struct_info was moved above
		type_spec = TypeSpecifierNode(
			Type::Struct,
			struct_type_index,
			static_cast<int>(struct_type_info.getStructInfo()->total_size * 8),
			Token(Token::Type::Identifier, StringTable::getStringView(struct_name_for_typedef), 0, 0, 0)
		);
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else {
		// Parse the underlying type normally
		auto type_result = parse_type_specifier();
		if (type_result.is_error()) {
			return type_result;
		}

		if (!type_result.node().has_value()) {
			return ParseResult::error("Expected type specifier after 'typedef'", current_token_);
		}

		type_node = *type_result.node();
		type_spec = type_node.as<TypeSpecifierNode>();

		// Handle pointer/reference declarators (ptr-operator in C++20 grammar)
		// consume_pointer_ref_modifiers handles *, cv-qualifiers, MSVC modifiers, &, &&
		consume_pointer_ref_modifiers(type_spec);
		
		// Check for pointer-to-member type syntax: typedef Type Class::* alias;
		// This is used in <type_traits> for result_of patterns
		// Pattern: typedef _Res _Class::* _MemPtr;
		if (peek().is_identifier()) {
			// Look ahead to see if this is Class::* pattern
			SaveHandle saved_pos = save_token_position();
			Token class_token = peek_info();
			advance(); // consume potential class name
			
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					// This is a pointer-to-member type: Type Class::*
					// Mark the type as a pointer-to-member
					type_spec.add_pointer_level(CVQualifier::None);  // Add pointer level
					type_spec.set_member_class_name(class_token.handle());
					FLASH_LOG(Parser, Debug, "Parsed pointer-to-member typedef: ", type_spec.token().value(), " ", class_token.value(), "::*");
					discard_saved_token(saved_pos);
				} else {
					// Not a pointer-to-member, restore position
					restore_token_position(saved_pos);
				}
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		}
	}

	// Check for function pointer typedef: typedef return_type (*alias_name)(params);
	// Pattern: '(' '*' identifier ')' '(' params ')'
	bool is_function_pointer_typedef = false;
	std::string_view function_pointer_alias_name;
	if (peek() == "("_tok) {
		// Peek ahead to check if this is a function pointer pattern
		SaveHandle paren_saved = save_token_position();
		advance(); // consume '('
		
		parse_calling_convention();
		
		if (peek() == "*"_tok) {
			advance(); // consume '*'
			
			// Now expect the alias name identifier
			skip_cpp_attributes();
			skip_gcc_attributes();
			if (peek().is_identifier()) {
				function_pointer_alias_name = peek_info().value();
				advance(); // consume alias name
				
				// Expect closing ')'
				if (peek() == ")"_tok) {
					advance(); // consume ')'
					
					// Now expect '(' for the parameter list
					if (peek() == "("_tok) {
						// This is a function pointer typedef!
						is_function_pointer_typedef = true;
						discard_saved_token(paren_saved);
						
						// Parse the parameter list
						advance(); // consume '('
						
						// Skip the parameter list by counting parentheses
						int paren_depth = 1;
						while (paren_depth > 0 && !peek().is_eof()) {
							auto token = peek_info();
							if (token.value() == "(") {
								paren_depth++;
							} else if (token.value() == ")") {
								paren_depth--;
							}
							advance();
						}
						
						// We've consumed through the closing ')' of the parameter list
					}
				}
			}
		}
		
		// If not a function pointer typedef, restore position
		if (!is_function_pointer_typedef) {
			restore_token_position(paren_saved);
		}
	}

	std::string_view alias_name;
	std::optional<Token> alias_token;
	
	if (is_function_pointer_typedef) {
		alias_name = function_pointer_alias_name;
		// Create a synthetic token for the alias name (use file index 0 since it's synthetic)
		alias_token = Token(Token::Type::Identifier, function_pointer_alias_name, 0, 0, 0);
		
		// For function pointer typedefs, create a proper FunctionPointer type
		// The return type is in type_spec, create a function pointer type with it
		Type return_type = type_spec.type();
		
		// Create a new TypeSpecifierNode for the function pointer (64-bit pointer)
		TypeSpecifierNode fp_type(Type::FunctionPointer, TypeQualifier::None, 64);
		
		// Create a basic function signature with the return type
		// Note: We don't have full parameter info here since we just skipped the param list
		// This is a simplified implementation that handles the common case
		FunctionSignature sig;
		sig.return_type = return_type;
		sig.linkage = Linkage::None;
		fp_type.set_function_signature(sig);
		
		// Replace type_spec with the function pointer type
		type_spec = fp_type;
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	} else {
		// Parse the alias name (identifier)
		alias_token = advance();
		if (!alias_token->kind().is_identifier()) {
			return ParseResult::error("Expected identifier after type in typedef", *alias_token);
		}
		alias_name = alias_token->value();
	}

	// Check for function type typedef: typedef return_type name(params);
	// This is different from function pointer typedef: typedef return_type (*name)(params);
	if (peek() == "("_tok) {
		// This is a function type typedef
		// Parse the parameter list by skipping to the closing ')'
		advance(); // consume '('
		
		int paren_depth = 1;
		while (paren_depth > 0 && !peek().is_eof()) {
			auto token = peek_info();
			if (token.value() == "(") {
				paren_depth++;
			} else if (token.value() == ")") {
				paren_depth--;
			}
			advance();
		}
		
		// After consuming the closing ')', we should be at the semicolon
		// (or potentially attribute specifiers, which we'll skip in the semicolon check)
	}

	// Check for array typedef: typedef type name[size];
	// This creates a type alias for an array type
	if (peek() == "["_tok) {
		// Parse array dimensions
		while (peek() == "["_tok) {
			advance(); // consume '['
			
			// Parse the array size expression
			ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}
			
			// Try to evaluate the array size using constexpr evaluator
			size_t array_size = 0;
			if (size_result.node().has_value()) {
				ConstExpr::EvaluationContext ctx(gSymbolTable);
				auto eval_result = ConstExpr::Evaluator::evaluate(*size_result.node(), ctx);
				if (eval_result.success() && eval_result.as_int() > 0) {
					array_size = static_cast<size_t>(eval_result.as_int());
				}
			}
			
			// Add array dimension to the type specifier
			type_spec.add_array_dimension(array_size);
			
			// Expect closing ']'
			if (!consume("]"_tok)) {
				return ParseResult::error("Expected ']' after array size in typedef", current_token_);
			}
		}
		
		// Update type_node with the array type
		type_node = emplace_node<TypeSpecifierNode>(type_spec);
	}

	// Skip any GCC attributes that might appear before the semicolon
	// e.g., typedef _Complex float __cfloat128 __attribute__ ((__mode__ (__TC__)));
	skip_cpp_attributes();

	// Expect semicolon
	if (!consume(";"_tok)) {
		return ParseResult::error("Expected ';' after typedef declaration", current_token_);
	}

	// Build the qualified name for the typedef if we're in a namespace
	std::string_view qualified_alias_name;
	NamespaceHandle namespace_handle = gSymbolTable.get_current_namespace_handle();
	if (!namespace_handle.isGlobal()) {
		StringHandle alias_handle = StringTable::getOrInternStringHandle(alias_name);
		StringHandle qualified_handle = gNamespaceRegistry.buildQualifiedIdentifier(namespace_handle, alias_handle);
		qualified_alias_name = StringTable::getStringView(qualified_handle);
	} else {
		qualified_alias_name = alias_name;
	}

	// Register the typedef alias in the type system
	// The typedef should resolve to the underlying type, not be a new UserDefined type
	// We create a TypeInfo entry that mirrors the underlying type
	auto& alias_type_info = gTypeInfo.emplace_back(StringTable::getOrInternStringHandle(qualified_alias_name), type_spec.type(), type_spec.type_index(), type_spec.size_in_bits());
	alias_type_info.pointer_depth_ = type_spec.pointer_depth();
	alias_type_info.is_reference_ = type_spec.is_reference();
	alias_type_info.is_rvalue_reference_ = type_spec.is_rvalue_reference();
	gTypesByName.emplace(alias_type_info.name(), &alias_type_info);

	// Update the type_node with the modified type_spec (with pointers)
	type_node = emplace_node<TypeSpecifierNode>(type_spec);

	// Create and return typedef declaration node
	auto typedef_node = emplace_node<TypedefDeclarationNode>(type_node, *alias_token);
	return saved_position.success(typedef_node);
}

