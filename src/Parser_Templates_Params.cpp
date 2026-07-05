#include "Parser.h"
#include "CallNodeHelpers.h"
#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "TypeTraitEvaluator.h"

namespace {
bool isBoolResultOperator(std::string_view op) {
	return op == "==" || op == "!=" ||
		   op == "<" || op == "<=" ||
		   op == ">" || op == ">=" ||
		   op == "&&" || op == "||";
}
}

// Parse noexcept or noexcept(expr) specifier and return the evaluated boolean value.
// Assumes the 'noexcept' token has already been consumed by the caller.
// - Bare 'noexcept' (no parens) → returns true.
// - 'noexcept(expr)' → tries to parse and evaluate expr as a constant expression.
//   If expr evaluates to 0/false, returns false. Otherwise (including dependent
//   expressions that cannot be evaluated), returns true conservatively.
bool Parser::parse_noexcept_value() {
	if (peek() != "("_tok) {
		// bare noexcept (no parens) is noexcept(true)
		return true;
	}
	// noexcept(expr) — try to evaluate; default to true for dependent exprs
	SaveHandle noexcept_expr_pos = save_token_position();
	advance(); // consume '('
	auto noexcept_expr_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
	if (!noexcept_expr_result.is_error() && noexcept_expr_result.node().has_value() && peek() == ")"_tok) {
		advance(); // consume ')'
		discard_saved_token(noexcept_expr_pos);
		auto eval = try_evaluate_constant_expression(*noexcept_expr_result.node());
		return !eval.has_value() || eval->value != 0;
	}
	// Expression parsing failed — fall back to skipping balanced parens
	restore_token_position(noexcept_expr_pos);
	skip_balanced_parens();
	return true; // dependent — assume true
}

ParseResult Parser::parse_template_parameter_list(InlineVector<TemplateParameterNode, 4>& out_params) {
	// Save and restore the current template parameter state that existed before
	// parsing this list so names added here do not persist after the parse.
	// ScopedStateCopy keeps the field populated so that outer template parameter
	// names remain visible while parsing inner parameters' default values (e.g.
	// template<typename T, bool = is_arithmetic<T>::value>).
	FlashCpp::ScopedStateCopy guard_template_params(currentTemplateParamState());

	bool first_parameter = true;
	while (first_parameter || peek() == ","_tok) {
		if (!first_parameter) {
			advance(); // consume ','
		}
		first_parameter = false;

		auto param_result = parse_template_parameter();
		if (param_result.is_error()) {
			return param_result;
		}

		if (param_result.node().has_value()) {
			if (!param_result.node()->is<TemplateParameterNode>()) {
				return ParseResult::error("Expected template parameter node", current_token_);
			}
			out_params.push_back(param_result.node()->as<TemplateParameterNode>());
			// Add this parameter's name so subsequent parameters can reference it in
			// their default values, e.g. template<typename T, bool = is_arithmetic<T>::value>.
			auto& tparam = out_params.back();
			const TypeCategory non_type_category =
				tparam.kind() == TemplateParameterKind::NonType && tparam.has_type()
				? tparam.type_specifier_node().type()
				: TypeCategory::Invalid;
			pushCurrentTemplateParameter(tparam.nameHandle(), tparam.kind(), non_type_category);
			if (tparam.kind() == TemplateParameterKind::Type ||
				tparam.kind() == TemplateParameterKind::Template) {
				ensureTemplateParameterTypeRegistration(tparam);
			}
			FLASH_LOG(Templates, Debug, "Added template parameter '", tparam.name(),
					  "' to current_template_param_names_ (now has ", currentTemplateParamCount(), " params)");
		}
	}

	return ParseResult::success();
}

TypeInfo& Parser::ensureTemplateParameterTypeRegistration(TemplateParameterNode& tparam) {
	if (tparam.kind() != TemplateParameterKind::Type &&
		tparam.kind() != TemplateParameterKind::Template) {
		throw InternalError("Template parameter type registration requires a type-like parameter");
	}

	TypeInfo* type_info = nullptr;
	if (tparam.registered_type_index().is_valid()) {
		type_info = tryGetTypeInfoMut(tparam.registered_type_index());
	}

	if (type_info == nullptr) {
		if (auto existing_it = getTypesByNameMap().find(tparam.nameHandle());
			existing_it != getTypesByNameMap().end() &&
			existing_it->second != nullptr &&
			existing_it->second->isDependentPlaceholder()) {
			type_info = existing_it->second;
		}
	}

	if (type_info == nullptr) {
		type_info = &add_template_param_type(
			tparam.nameHandle(),
			tparam.kind() == TemplateParameterKind::Template ? TypeCategory::Template : TypeCategory::UserDefined,
			0);
	}

	type_info->placeholder_kind_ = DependentPlaceholderKind::DependentArgs;
	tparam.set_registered_type_index(type_info->type_index_);
	return *type_info;
}

Parser::TemplateParameterMetadata Parser::registerTemplateParametersInScope(
	InlineVector<TemplateParameterNode, 4>& template_params,
	FlashCpp::TemplateParameterScope& template_scope) {
	TemplateParameterMetadata metadata;
	for (TemplateParameterNode& tparam : template_params) {
		metadata.names.push_back(tparam.nameHandle());
		metadata.kinds.push_back(tparam.kind());
		metadata.non_type_categories.push_back(
			tparam.kind() == TemplateParameterKind::NonType && tparam.has_type()
			? tparam.type_specifier_node().type()
			: TypeCategory::Invalid);
		metadata.has_packs |= tparam.is_variadic();
		if (tparam.kind() == TemplateParameterKind::Type ||
			tparam.kind() == TemplateParameterKind::Template) {
			TypeInfo& type_info = ensureTemplateParameterTypeRegistration(tparam);
			template_scope.addParameter(&type_info);
		}
	}
	return metadata;
}

// Parse a single template parameter: typename T, class T, int N, etc.
ParseResult Parser::parse_template_parameter() {
	ScopedTokenPosition saved_position(*this);

	// Check for template template parameter: template<template<typename> class Container>
	if (peek() == "template"_tok) {
		[[maybe_unused]] Token template_keyword = peek_info();
		advance(); // consume 'template'

		// Expect '<' to start nested template parameter list
		if (peek() != "<"_tok) {
			FLASH_LOG(Parser, Error, "Expected '<' after 'template', got: ",
					  (!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected '<' after 'template' keyword in template template parameter", current_token_);
		}
		advance(); // consume '<'

		// Parse nested template parameter forms (just type specifiers, no names)
		InlineVector<TemplateParameterNode, 4> nested_params;
		auto param_list_result = parse_template_template_parameter_forms(nested_params);
		if (param_list_result.is_error()) {
			FLASH_LOG(Parser, Error, "parse_template_template_parameter_forms failed");
			return param_list_result;
		}

		// Expect '>' to close nested template parameter list
		if (peek() != ">"_tok) {
			FLASH_LOG(Parser, Error, "Expected '>' after nested template parameter list, got: ",
					  (!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected '>' after nested template parameter list", current_token_);
		}
		advance(); // consume '>'

		// Expect 'class' or 'typename'
		if (!peek().is_keyword() ||
			(peek() != "class"_tok && peek() != "typename"_tok)) {
			FLASH_LOG(Parser, Error, "Expected 'class' or 'typename' after template parameter list, got: ",
					  (!peek().is_eof() ? std::string("'") + std::string(peek_info().value()) + "'" : "<EOF>"));
			return ParseResult::error("Expected 'class' or 'typename' after template parameter list in template template parameter", current_token_);
		}
		advance(); // consume 'class' or 'typename'

		// Parameter name is optional (unnamed template template parameters are valid C++)
		// e.g., template <class, class, template <class> class, template <class> class>
		std::string_view param_name;
		Token param_name_token;
		if (peek().is_identifier()) {
			param_name_token = peek_info();
			param_name = param_name_token.value();
			advance(); // consume parameter name
		} else {
			// Generate a unique synthetic name for unnamed template template parameter.
			// This avoids collisions when multiple unnamed template template parameters
			// appear in the same declaration (e.g., template<template<class> class, template<class> class>).
			// Without unique names, substitution maps would overwrite earlier bindings.
			static int anonymous_template_template_counter = 0;
			param_name = StringBuilder().append("__anon_ttp_"sv).append(static_cast<int64_t>(anonymous_template_template_counter++)).commit();
			param_name_token = current_token_;
		}

		// Create template template parameter node
		auto param_node = emplace_node<TemplateParameterNode>(
			StringTable::getOrInternStringHandle(param_name),
			std::span<const TemplateParameterNode>(nested_params.data(), nested_params.size()),
			param_name_token);

		// Handle default arguments (e.g., template<typename> class Container = std::vector)
		if (peek() == "="_tok) {
			advance(); // consume '='

			// Save position for potential SFINAE re-parse of dependent defaults
			SaveHandle default_pos = save_token_position();

			// Parse the default type.
			// parse_type_specifier() handles both simple names (MyContainer) and
			// namespace-qualified names (my_ns::NsContainer, std::vector) via
			// its qualified identifier resolution infrastructure.
			ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::ShapeOnly, StringHandle{});
			auto default_type_result = parse_type_specifier();
			if (default_type_result.is_error()) {
				discard_saved_token(default_pos);
				return ParseResult::error("Expected type after '=' in template template parameter default", current_token_);
			}

			if (default_type_result.node().has_value()) {
				param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
				param_node.as<TemplateParameterNode>().set_default_value_position(default_pos);
			} else {
				discard_saved_token(default_pos);
			}
		}

		return saved_position.success(param_node);
	}

	// Check for concept-constrained type parameter: Concept T, Concept<U> T, namespace::Concept T
	if (peek().is_identifier()) {
		auto concept_check_pos = save_token_position();

		// Build potential concept name (possibly namespace-qualified)
		StringBuilder potential_concept_sb;
		potential_concept_sb.append(peek_info().value());
		Token concept_token = peek_info();
		advance(); // consume first identifier

		// Check for namespace-qualified concept: ns::concept or ns::ns2::concept
		while (peek() == "::"_tok) {
			advance(); // consume '::'
			if (!peek().is_identifier()) {
				// Not a valid qualified name, restore and continue
				restore_token_position(concept_check_pos);
				potential_concept_sb.reset();
				break;
			}
			potential_concept_sb.append("::");
			potential_concept_sb.append(peek_info().value());
			concept_token = peek_info();
			advance(); // consume next identifier
		}

		// Intern the concept name string and get a stable string_view
		StringHandle concept_handle = StringTable::getOrInternStringHandle(potential_concept_sb);
		std::string_view potential_concept = StringTable::getStringView(concept_handle);

		// Check if this identifier is a registered concept
		FLASH_LOG_FORMAT(Parser, Debug, "parse_template_parameter: Checking if '{}' is a concept", potential_concept);
		if (gConceptRegistry.hasConcept(potential_concept)) {
			FLASH_LOG_FORMAT(Parser, Debug, "parse_template_parameter: '{}' IS a registered concept", potential_concept);
			// Check for template arguments: Concept<U>
			std::vector<ASTNode> concept_template_args;
			if (peek() == "<"_tok) {
				advance(); // consume '<'
				// Parse concept template arguments as type specifiers
				while (peek() != ">"_tok && !peek().is_eof()) {
					auto arg_result = parse_type_specifier();
					if (!arg_result.is_error() && arg_result.node().has_value()) {
						concept_template_args.push_back(*arg_result.node());
					} else {
						// Skip unrecognized token with a diagnostic
						FLASH_LOG_FORMAT(Parser, Warning, "Skipping unrecognized token '{}' in concept template arguments for '{}'",
										 !peek().is_eof() ? std::string(peek_info().value()) : "<EOF>", potential_concept);
						advance();
					}
					if (peek() == ","_tok) {
						advance(); // consume ','
					}
				}
				if (peek() == ">"_tok) {
					advance(); // consume '>'
				}
			}

			// Check for ellipsis (parameter pack): Concept... Ts
			bool is_variadic = false;
			if (!peek().is_eof() &&
				(peek().is_operator() || peek().is_punctuator()) &&
				peek() == "..."_tok) {
				advance(); // consume '...'
				is_variadic = true;
			}

			// Expect identifier (parameter name)
			if (!peek().is_identifier()) {
				auto param_node = emplace_node<TemplateParameterNode>(StringHandle(), concept_token);
				param_node.as<TemplateParameterNode>().set_concept_constraint(potential_concept);
				if (!concept_template_args.empty()) {
					param_node.as<TemplateParameterNode>().set_concept_args(
						std::move(concept_template_args));
				}
				if (is_variadic) {
					param_node.as<TemplateParameterNode>().set_variadic(true);
				}
				if (!is_variadic && peek() == "="_tok) {
					advance(); // consume '='

					ScopedParserInstantiationContext guard_instantiation_mode(
						*this,
						TemplateInstantiationMode::ShapeOnly,
						StringHandle{});
					auto default_type_result = parse_type_specifier();
					if (default_type_result.is_error()) {
						return default_type_result;
					}
					if (!default_type_result.node().has_value()) {
						return ParseResult::error(
							"Expected default argument after '=' in template parameter",
							current_token_);
					}
					param_node.as<TemplateParameterNode>().set_default_value(
						*default_type_result.node());
				}
				return saved_position.success(param_node);
			}

			Token param_name_token = peek_info();
			std::string_view param_name = param_name_token.value();
			advance(); // consume parameter name

			// Create type parameter node (concept-constrained)
			auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), param_name_token);

			// Store the concept constraint
			param_node.as<TemplateParameterNode>().set_concept_constraint(potential_concept);

			// Store concept template arguments if present (e.g., Concept<U> T stores {U})
			if (!concept_template_args.empty()) {
				param_node.as<TemplateParameterNode>().set_concept_args(std::move(concept_template_args));
			}

			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}

			// Handle default arguments (e.g., Concept T = int)
			// Note: Parameter packs cannot have default arguments
			if (!is_variadic && peek() == "="_tok) {
				advance(); // consume '='

				// Save position for potential SFINAE re-parse of dependent defaults
				SaveHandle default_pos = save_token_position();

				// Parse the default type
				ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::ShapeOnly, StringHandle{});
				auto default_type_result = parse_type_specifier();
				if (default_type_result.is_error()) {
					discard_saved_token(default_pos);
					return ParseResult::error("Expected type after '=' in template parameter default", current_token_);
				}

				if (default_type_result.node().has_value()) {
					TypeSpecifierNode& type_spec = default_type_result.node()->as<TypeSpecifierNode>();

					// Apply pointer/reference qualifiers (ptr-operator in C++20 grammar)
					consume_pointer_ref_modifiers(type_spec);
					param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
					param_node.as<TemplateParameterNode>().set_default_value_position(default_pos);
				} else {
					discard_saved_token(default_pos);
				}
			}

			return saved_position.success(param_node);
		} else {
			// Not a concept, restore position and let other parsing handle it
			restore_token_position(concept_check_pos);
		}
	}

	// Check for type parameter: typename or class
	if (peek().is_keyword()) {
		std::string_view keyword = peek_info().value();

		if (keyword == "typename" || keyword == "class") {
			[[maybe_unused]] Token keyword_token = peek_info();
			advance(); // consume 'typename' or 'class'

			// Check for ellipsis (parameter pack): typename... Args
			bool is_variadic = false;
			if (!peek().is_eof() &&
				(peek().is_operator() || peek().is_punctuator()) &&
				peek() == "..."_tok) {
				advance(); // consume '...'
				is_variadic = true;
			}

			// Check for identifier (parameter name) - it's optional for anonymous parameters
			std::string_view param_name;
			Token param_name_token;

			if (peek().is_identifier()) {
				// Named parameter
				param_name_token = peek_info();
				param_name = param_name_token.value();
				advance(); // consume parameter name
			} else {
				// Anonymous parameter - generate unique name
				// Check if next token is valid for end of parameter (comma, >, or =)
				if (!peek().is_eof() &&
					((peek().is_punctuator() && peek() == ","_tok) ||
					 (peek().is_operator() && (peek() == ">"_tok || peek() == "="_tok)))) {
					// Generate unique anonymous parameter name
					static int anonymous_type_counter = 0;
					param_name = StringBuilder().append("__anon_type_"sv).append(static_cast<int64_t>(anonymous_type_counter++)).commit();

					// Use the current token as the token reference
					param_name_token = current_token_;
				} else {
					return ParseResult::error("Expected identifier after 'typename' or 'class'", current_token_);
				}
			}

			// Create type parameter node
			auto param_node = emplace_node<TemplateParameterNode>(StringTable::getOrInternStringHandle(param_name), param_name_token);

			// Set variadic flag if this is a parameter pack
			if (is_variadic) {
				param_node.as<TemplateParameterNode>().set_variadic(true);
			}

			// Handle default arguments (e.g., typename T = int)
			// Note: Parameter packs cannot have default arguments
			if (!is_variadic && peek() == "="_tok) {
				advance(); // consume '='

				// Save position for potential SFINAE re-parse of dependent defaults
				SaveHandle default_pos = save_token_position();

				// Parse the default type
				ScopedParserInstantiationContext guard_instantiation_mode(*this, TemplateInstantiationMode::ShapeOnly, StringHandle{});
				auto default_type_result = parse_type_specifier();
				if (default_type_result.is_error()) {
					discard_saved_token(default_pos);
					return ParseResult::error("Expected type after '=' in template parameter default", current_token_);
				}

				if (default_type_result.node().has_value()) {
					TypeSpecifierNode& type_spec = default_type_result.node()->as<TypeSpecifierNode>();

					// Apply pointer/reference qualifiers (ptr-operator in C++20 grammar)
					consume_pointer_ref_modifiers(type_spec);
					param_node.as<TemplateParameterNode>().set_default_value(*default_type_result.node());
					param_node.as<TemplateParameterNode>().set_default_value_position(default_pos);
				} else {
					discard_saved_token(default_pos);
				}
			}

			return saved_position.success(param_node);
		}
	}

	// Check for non-type parameter: int N, bool B, etc.
	// Parse type specifier
	auto type_result = parse_type_specifier();
	if (type_result.is_error()) {
		return type_result;
	}

	if (!type_result.node().has_value()) {
		return ParseResult::error("Expected type specifier for non-type template parameter", current_token_);
	}
		TypeSpecifierNode nttp_type = type_result.node()->as<TypeSpecifierNode>();
		consume_pointer_ref_modifiers(nttp_type);
		if (nttp_type.is_reference() &&
			nttp_type.type() != TypeCategory::Auto &&
			nttp_type.type() != TypeCategory::DeclTypeAuto) {
			return ParseResult::error(
				"Reference non-type template parameters are not supported yet",
				type_result.node()->as<TypeSpecifierNode>().token());
		}
		if ((nttp_type.type() == TypeCategory::Struct ||
			 nttp_type.type() == TypeCategory::UserDefined ||
			 nttp_type.type() == TypeCategory::TypeAlias) &&
		nttp_type.type_index().is_valid()) {
		const TypeInfo* nttp_type_info = tryGetTypeInfo(nttp_type.type_index());
		if (nttp_type_info != nullptr &&
			!nttp_type_info->isDependentPlaceholder() &&
			nttp_type_info->getStructInfo() != nullptr) {
			return ParseResult::error(
				"Structural class-type non-type template parameters are not supported yet",
				type_result.node()->as<TypeSpecifierNode>().token());
		}
	}

	// Check for ellipsis (parameter pack): int... Ns
	bool is_variadic = false;
	if (!peek().is_eof() &&
		(peek().is_operator() || peek().is_punctuator()) &&
		peek() == "..."_tok) {
		advance(); // consume '...'
		is_variadic = true;
	}
	// Check for identifier (parameter name) - it's optional for anonymous parameters
	std::string_view param_name;
	Token param_name_token;
	[[maybe_unused]] bool is_anonymous = false;

	// Check for function pointer declarator syntax: T (*Name)(params...)
	// e.g., template <int (*F)()> or template <void (*)(int, double)>
	// Also handles member function pointer: T (Class::*Name)(params...)
	// e.g., template <int (S::*MemberFn)()>
	// The base type specifier (e.g. 'int') has already been parsed into nttp_type.
	// We now look for the (*Name)(...) or (Class::*Name)(...) declarator group.
	bool parsed_as_function_pointer = false;
	if (!is_variadic && peek() == "("_tok) {
		SaveHandle fp_decl_pos = save_token_position();
		advance(); // consume '('

		// Skip optional calling convention (__cdecl, __stdcall, etc.) before '*'
		CallingConvention fp_calling_convention = parse_calling_convention(CallingConvention::Default);

		if (peek() == "*"_tok) {
			advance(); // consume '*'

			// Parse the optional parameter name inside (*Name), e.g. 'F' in int (*F)()
			Token fp_name_token;
			std::string_view fp_name;
			if (peek().is_identifier()) {
				fp_name_token = peek_info();
				fp_name = fp_name_token.value();
				advance(); // consume the name
			}

			if (peek() == ")"_tok) {
				advance(); // consume ')'

				// Now expect the argument type list: (params...)
				if (peek() == "("_tok) {
					advance(); // consume '('

					std::vector<TypeIndex> fp_param_types;
					bool param_parse_ok = parse_function_type_parameter_list(fp_param_types);

					if (param_parse_ok && peek() == ")"_tok) {
						advance(); // consume ')'

						// Parse optional noexcept specifier after parameter list
						bool sig_is_noexcept = false;
						if (peek() == "noexcept"_tok) {
							advance(); // consume 'noexcept'
							sig_is_noexcept = parse_noexcept_value();
						}

						// Build the function signature: the return type is nttp_type as currently
						// parsed (e.g. 'int' for int (*F)()).
						FunctionSignature func_sig;
						func_sig.return_type_index = nttp_type.type_index();
						func_sig.return_pointer_depth = static_cast<int>(nttp_type.pointer_depth());
						func_sig.return_reference_qualifier = nttp_type.reference_qualifier();
						func_sig.parameter_type_indices = std::move(fp_param_types);
						func_sig.calling_convention = fp_calling_convention;
						func_sig.is_noexcept = sig_is_noexcept;

						// Rewrite nttp_type to TypeCategory::FunctionPointer.
						// FunctionPointer is intrinsically pointer-sized; the "pointer-ness" is
						// baked into the category (no extra pointer_level needed).
						nttp_type.set_type_index(nativeTypeIndex(TypeCategory::FunctionPointer));
						nttp_type.set_size_in_bits(64);
						nttp_type.limit_pointer_depth(0);
						nttp_type.set_function_signature(func_sig);

						discard_saved_token(fp_decl_pos);
						parsed_as_function_pointer = true;

						// Use the name from (*Name), or generate an anonymous name
						if (!fp_name.empty()) {
							param_name = fp_name;
							param_name_token = fp_name_token;
						} else {
							static int anonymous_fp_counter = 0;
							param_name = StringBuilder()
								.append("__anon_fp_param_"sv)
								.append(static_cast<int64_t>(anonymous_fp_counter++))
								.commit();
							param_name_token = current_token_;
							is_anonymous = true;
						}
					} else {
						restore_token_position(fp_decl_pos);
					}
				} else {
					restore_token_position(fp_decl_pos);
				}
			} else {
				restore_token_position(fp_decl_pos);
			}
		} else if (peek().is_identifier()) {
			// Check for member function pointer pattern: (ClassName::*Name)(params...)
			// e.g., template <int (S::* MemberFn)()>
			Token mfp_class_token = peek_info();
			advance(); // consume class name
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'

					// Parse optional parameter name inside (ClassName::*Name)
					Token mfp_name_token;
					std::string_view mfp_name;
					if (peek().is_identifier()) {
						mfp_name_token = peek_info();
						mfp_name = mfp_name_token.value();
						advance(); // consume the name
					}

					if (peek() == ")"_tok) {
						advance(); // consume ')'

						// Expect the argument type list: (params...)
						if (peek() == "("_tok) {
							advance(); // consume '('

							std::vector<TypeIndex> mfp_param_types;
							bool mfp_param_ok = parse_function_type_parameter_list(mfp_param_types);

							if (mfp_param_ok && peek() == ")"_tok) {
								advance(); // consume ')'

								// Parse optional cv-qualifiers, ref-qualifier, noexcept
								// e.g., template <int (S::* F)() const noexcept>
								bool mfp_is_const = false;
								bool mfp_is_volatile = false;
								ReferenceQualifier mfp_ref_qual = ReferenceQualifier::None;
								bool mfp_is_noexcept = false;
								while (!peek().is_eof()) {
									if (peek() == "const"_tok) {
										mfp_is_const = true;
										advance();
									} else if (peek() == "volatile"_tok) {
										mfp_is_volatile = true;
										advance();
									} else if (peek() == "&"_tok) {
										mfp_ref_qual = ReferenceQualifier::LValueReference;
										advance();
									} else if (peek() == "&&"_tok) {
										mfp_ref_qual = ReferenceQualifier::RValueReference;
										advance();
									} else if (peek() == "noexcept"_tok) {
										advance();
										mfp_is_noexcept = parse_noexcept_value();
									} else {
										break;
									}
								}

								// Build function signature for the member function pointer
								FunctionSignature mfp_sig;
								mfp_sig.return_type_index = nttp_type.type_index();
								mfp_sig.return_pointer_depth = static_cast<int>(nttp_type.pointer_depth());
								mfp_sig.return_reference_qualifier = nttp_type.reference_qualifier();
								mfp_sig.parameter_type_indices = std::move(mfp_param_types);
								mfp_sig.calling_convention = fp_calling_convention;
								mfp_sig.is_const = mfp_is_const;
								mfp_sig.is_volatile = mfp_is_volatile;
								mfp_sig.function_reference_qualifier = mfp_ref_qual;
								mfp_sig.is_noexcept = mfp_is_noexcept;

								// Rewrite nttp_type as MemberFunctionPointer
								nttp_type.set_type_index(nativeTypeIndex(TypeCategory::MemberFunctionPointer));
								nttp_type.set_size_in_bits(64);
								nttp_type.limit_pointer_depth(0);
								nttp_type.set_function_signature(mfp_sig);
								nttp_type.set_member_class_name(mfp_class_token.handle());

								discard_saved_token(fp_decl_pos);
								parsed_as_function_pointer = true;

								// Use the name from (ClassName::*Name), or generate anonymous
								if (!mfp_name.empty()) {
									param_name = mfp_name;
									param_name_token = mfp_name_token;
								} else {
									static int anonymous_mfp_counter = 0;
									param_name = StringBuilder()
										.append("__anon_mfp_param_"sv)
										.append(static_cast<int64_t>(anonymous_mfp_counter++))
										.commit();
									param_name_token = current_token_;
									is_anonymous = true;
								}
							} else {
								restore_token_position(fp_decl_pos);
							}
						} else {
							restore_token_position(fp_decl_pos);
						}
					} else {
						restore_token_position(fp_decl_pos);
					}
				} else {
					restore_token_position(fp_decl_pos);
				}
			} else {
				restore_token_position(fp_decl_pos);
			}
		} else {
			restore_token_position(fp_decl_pos);
		}
	}

	// Check for member object pointer NTTP: BaseType ClassName::* param_name
	// e.g., template <int S::* Member>
	// This must come after the function pointer check (which handles '(' first).
	// After consume_pointer_ref_modifiers, peek should be the class name identifier.
	if (!is_variadic && !parsed_as_function_pointer && peek().is_identifier()) {
		SaveHandle mop_pos = save_token_position();
		Token mop_class_token = peek_info();
		advance(); // consume potential class name
		if (peek() == "::"_tok) {
			advance(); // consume '::'
			if (peek() == "*"_tok) {
				advance(); // consume '*'
				// This is T ClassName::* — member object pointer NTTP
				// Rewrite nttp_type as MemberObjectPointer (64-bit, no extra pointer level)
				nttp_type.set_type_index(nativeTypeIndex(TypeCategory::MemberObjectPointer));
				nttp_type.set_size_in_bits(64);
				nttp_type.limit_pointer_depth(0);
				nttp_type.set_member_class_name(mop_class_token.handle());
				discard_saved_token(mop_pos);
				FLASH_LOG(Parser, Debug, "Parsed member object pointer NTTP: ",
						  nttp_type.token().value(), " ", mop_class_token.value(), "::*");
				// Fall through to parameter name parsing below
			} else {
				restore_token_position(mop_pos);
			}
		} else {
			restore_token_position(mop_pos);
		}
	}

	if (!parsed_as_function_pointer) {
		if (peek().is_identifier()) {
			// Named parameter
			param_name_token = peek_info();
			param_name = param_name_token.value();
			advance(); // consume parameter name
		} else {
			// Anonymous parameter - generate unique name
			// Check if next token is valid for end of parameter (comma, >, or =)
			if (!peek().is_eof() &&
				(peek() == ","_tok || peek() == ">"_tok || peek() == ">>"_tok || peek() == "="_tok)) {
				// Generate unique anonymous parameter name
				static int anonymous_counter = 0;
				param_name = StringBuilder().append("__anon_param_"sv).append(static_cast<int64_t>(anonymous_counter++)).commit();

				// Store the anonymous name in a way that persists
				// We'll use the current token as the token reference
				param_name_token = current_token_;
				is_anonymous = true;
			} else {
				return ParseResult::error("Expected identifier for non-type template parameter", current_token_);
			}
		}
	}

	// Create non-type parameter node
	auto param_node = emplace_node<TemplateParameterNode>(
		StringTable::getOrInternStringHandle(param_name),
		nttp_type,
		param_name_token);

	// Set variadic flag if this is a parameter pack
	if (is_variadic) {
		param_node.as<TemplateParameterNode>().set_variadic(true);
	}

	// Handle default arguments (e.g., int N = 10, size_t M = sizeof(T))
	// Note: Parameter packs cannot have default arguments
	if (!is_variadic && peek() == "="_tok) {
		advance(); // consume '='

		// Save position for potential SFINAE re-parse of dependent defaults
		SaveHandle default_pos = save_token_position();

		// Parse the default value expression in template argument context
		// This context tells parse_expression to stop at '>' and ',' which delimit template arguments
		auto default_value_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::TemplateTypeArg);
		if (default_value_result.is_error()) {
			discard_saved_token(default_pos);
			return ParseResult::error("Expected expression after '=' in template parameter default", current_token_);
		}

		if (default_value_result.node().has_value()) {
			param_node.as<TemplateParameterNode>().set_default_value(*default_value_result.node());
			param_node.as<TemplateParameterNode>().set_default_value_position(default_pos);
		} else {
			discard_saved_token(default_pos);
		}
	}

	return saved_position.success(param_node);
}

// Parse template template parameter forms (just type specifiers without names)
// Used for template<template<typename> class Container> syntax
ParseResult Parser::parse_template_template_parameter_forms(InlineVector<TemplateParameterNode, 4>& out_params) {
	// Parse first parameter form
	auto param_result = parse_template_template_parameter_form();
	if (param_result.is_error()) {
		return param_result;
	}

	if (param_result.node().has_value()) {
		if (!param_result.node()->is<TemplateParameterNode>()) {
			return ParseResult::error("Expected template parameter node", current_token_);
		}
		out_params.push_back(param_result.node()->as<TemplateParameterNode>());
	}

	// Parse additional parameter forms separated by commas
	while (peek() == ","_tok) {
		advance(); // consume ','

		param_result = parse_template_template_parameter_form();
		if (param_result.is_error()) {
			return param_result;
		}

		if (param_result.node().has_value()) {
			if (!param_result.node()->is<TemplateParameterNode>()) {
				return ParseResult::error("Expected template parameter node", current_token_);
			}
			out_params.push_back(param_result.node()->as<TemplateParameterNode>());
		}
	}

	return ParseResult::success();
}

// Parse a single template template parameter form (just type specifier, no name)
// For template<template<typename> class Container>, this parses "typename"
// Also handles variadic packs: template<typename...> class Container
// Also handles nested template template parameters: template<template<typename> class> class TTT
// Also handles non-type parameters: template<typename, int> class W (C++20 standard conforming)
ParseResult Parser::parse_template_template_parameter_form() {
	ScopedTokenPosition saved_position(*this);

	// Handle nested template template parameter: template<template<typename> class> class TTT
	if (peek().is_keyword() && peek() == "template"_tok) {
		return saved_position.propagate(parse_template_parameter());
	}

	// Helper: consume an optional ellipsis pack token and return whether it was present.
	const auto consumeOptionalEllipsis = [&]() -> bool {
		if (!peek().is_eof() &&
			(peek().is_operator() || peek().is_punctuator()) &&
			peek() == "..."_tok) {
			advance(); // consume '...'
			return true;
		}
		return false;
	};

	// Helper: consume an optional identifier parameter name (names are optional in TTP forms).
	const auto consumeOptionalName = [&]() {
		if (peek().is_identifier()) {
			advance(); // consume optional name
		}
	};

	// Handle type parameters: typename or class
	// BUT only when not followed by 'identifier ::' which indicates a dependent type
	// used as the type of a non-type parameter (e.g. 'typename T::type N') per C++20 [temp.param].
	if (peek().is_keyword()) {
		std::string_view keyword = peek_info().value();

		if (keyword == "typename" || keyword == "class") {
			SaveHandle keyword_save = save_token_position();
			Token keyword_token = peek_info();
			advance(); // tentatively consume 'typename' or 'class'

			// Look ahead: 'identifier ::' means this is a dependent type specifier
			// (e.g. 'typename T::type N') — a non-type parameter. Fall through in that case.
			bool is_dependent_type = false;
			if (peek().is_identifier()) {
				SaveHandle lookahead = save_token_position();
				advance(); // tentatively consume identifier
				is_dependent_type = (peek() == "::"_tok);
				restore_token_position(lookahead);
			}

			if (!is_dependent_type) {
				discard_saved_token(keyword_save);
				bool is_variadic = consumeOptionalEllipsis();
				consumeOptionalName();

				// Create a type parameter node with an empty name (form only)
				auto param_node = emplace_node<TemplateParameterNode>(StringHandle(), keyword_token);

				if (is_variadic) {
					param_node.as<TemplateParameterNode>().set_variadic(true);
				}

				return saved_position.success(param_node);
			}

			// Dependent type: restore to before 'typename'/'class' and fall through
			// to parse_type_specifier() which handles 'typename T::type'.
			restore_token_position(keyword_save);
		}
	}

	// Handle non-type parameters: int N, bool B, auto V, size_t S, etc.
	// Also handles dependent types: typename T::type N (C++20 [temp.param]).
	auto type_result = parse_type_specifier();
	if (!type_result.is_error() && type_result.node().has_value()) {
		bool is_variadic = consumeOptionalEllipsis();

		// Use the type specifier's token as the anchor; switch to the name token if present.
		Token anchor_token = type_result.node()->as<TypeSpecifierNode>().token();
		if (peek().is_identifier()) {
			anchor_token = peek_info();
			advance(); // consume optional name
		}

		// Create a non-type parameter node with an empty name (form only)
		auto param_node = emplace_node<TemplateParameterNode>(
			StringHandle(),
			type_result.node()->as<TypeSpecifierNode>(),
			anchor_token);

		if (is_variadic) {
			param_node.as<TemplateParameterNode>().set_variadic(true);
		}

		return saved_position.success(param_node);
	}

	return ParseResult::error("Expected 'typename', 'class', or type in template template parameter form", current_token_);
}

// Phase 6: Shared helper for template function declaration parsing
// This eliminates duplication between parse_template_declaration() and parse_member_function_template()
// Parses: type_and_name + function_declaration + body handling (semicolon or skip braces)
// Template parameters must already be registered in getTypesByNameMap() via TemplateParameterScope
std::optional<InlineVector<TemplateTypeArg, 4>> Parser::parse_explicit_template_arguments() {
	// Keep the no-output overload explicit so callers do not rely on default parameters.
	std::vector<ASTNode>* out_type_nodes = nullptr;
	return parse_explicit_template_arguments(out_type_nodes);
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::parse_explicit_template_arguments(std::vector<ASTNode>* out_type_nodes) {
	// Recursion depth guard to prevent stack overflow on deeply nested template arguments
	// Stack size increased to 8MB in FlashCppMSVC.vcxproj to handle deep recursion
	static thread_local int template_arg_recursion_depth = 0;
	constexpr int MAX_TEMPLATE_ARG_RECURSION_DEPTH = 20;

	struct TemplateArgRecursionGuard {
		int& depth;
		TemplateArgRecursionGuard(int& d) : depth(d) { ++depth; }
		~TemplateArgRecursionGuard() { --depth; }
	} guard(template_arg_recursion_depth);

	if (template_arg_recursion_depth > MAX_TEMPLATE_ARG_RECURSION_DEPTH) {
		FLASH_LOG_FORMAT(Templates, Error, "Hit MAX_TEMPLATE_ARG_RECURSION_DEPTH limit ({}) in parse_explicit_template_arguments", MAX_TEMPLATE_ARG_RECURSION_DEPTH);
		return std::nullopt;
	}

	FLASH_LOG_FORMAT(
		Templates,
		Debug,
		"parse_explicit_template_arguments called, sfinae_probe={}",
		template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe);

	// Save position in case this isn't template arguments
	auto saved_pos = save_token_position();

	// Check for '<'
	if (peek() != "<"_tok) {
		return std::nullopt;
	}

	// Prevent infinite loop: don't retry template argument parsing at the same position
	if (saved_pos == last_failed_template_arg_parse_handle_) {
		return std::nullopt;
	}

	advance(); // consume '<'
	last_failed_template_arg_parse_handle_ = SIZE_MAX;  // Clear failure marker - we're making progress

	InlineVector<TemplateTypeArg, 4> template_args;

	// Check for empty template argument list (e.g., Container<>)
	// Also handle >> for nested templates: Container<__void_t<>>
	if (peek() == ">"_tok) {
		advance(); // consume '>'
		// Success - discard saved position
		discard_saved_token(saved_pos);
		return template_args;
	}

	// Handle >> token for empty template arguments in nested context (e.g., __void_t<>>)
	if (peek() == ">>"_tok) {
		FLASH_LOG(Parser, Debug, "Empty template argument list with >> token, splitting");
		split_right_shift_token();
		// Now peek() returns '>'
		if (peek() == ">"_tok) {
			advance(); // consume first '>'
			discard_saved_token(saved_pos);
			return template_args;
		}
	}

	// Parse template arguments
	enum class SimpleTemplateArgKind {
		NotSimple,
		TypeLike,
		ValueLike,
		Unknown,
	};

	// Classifies a simple identifier name and – when it turns out to be ValueLike due to a
	// concrete substitution – also returns the already-built TemplateTypeArg so the caller
	// does not need a second loop over template_param_substitutions_.
	auto classifySimpleTemplateArgName = [&](StringHandle name_handle)
		-> std::pair<SimpleTemplateArgKind, std::optional<TemplateTypeArg>> {
		if (auto param_kind = currentTemplateParamKind(name_handle)) {
			if (*param_kind != TemplateParameterKind::NonType) {
				return { SimpleTemplateArgKind::TypeLike, std::nullopt };
			}
			// NonType param – also scan substitutions so we can return a concrete arg in one pass
			for (const auto& subst : template_param_substitutions_) {
				if (subst.param_name == name_handle && subst.is_value_param) {
					return { SimpleTemplateArgKind::ValueLike, TemplateTypeArg::makeValue(subst.value, subst.value_type) };
				}
			}
			return { SimpleTemplateArgKind::ValueLike, std::nullopt };
		}

		for (const auto& subst : template_param_substitutions_) {
			if (subst.param_name != name_handle) {
				continue;
			}
			if (subst.is_value_param) {
				return { SimpleTemplateArgKind::ValueLike, TemplateTypeArg::makeValue(subst.value, subst.value_type) };
			}
			if (subst.is_type_param || subst.is_template_template_param) {
				return { SimpleTemplateArgKind::TypeLike, std::nullopt };
			}
		}

		if (std::optional<ASTNode> symbol_lookup = lookup_symbol_with_template_check(name_handle);
			symbol_lookup.has_value()) {
			if (symbol_lookup->is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& variable_decl = symbol_lookup->as<VariableDeclarationNode>();
				if (variable_decl.initializer().has_value()) {
					if (auto const_value = try_evaluate_constant_expression(*variable_decl.initializer())) {
						TemplateTypeArg value_arg;
						value_arg.is_value = true;
						value_arg.value = const_value->value;
						value_arg.type_index = const_value->type_index.category() != TypeCategory::Invalid
							? const_value->type_index
							: nativeTypeIndex(const_value->type).withCategory(const_value->type);
						return { SimpleTemplateArgKind::ValueLike, value_arg };
					}
				}
			}
			if (symbol_lookup->is<ExpressionNode>()) {
				const ExpressionNode& lookup_expr = symbol_lookup->as<ExpressionNode>();
				if (std::holds_alternative<TemplateParameterReferenceNode>(lookup_expr) &&
					findTypeByName(name_handle) != nullptr) {
					return { SimpleTemplateArgKind::TypeLike, std::nullopt };
				}
			}
			return { SimpleTemplateArgKind::ValueLike, std::nullopt };
		}

		TemplateNameLookupRequest ordinary_template_lookup_request =
			buildTemplateNameLookupRequest(
				name_handle,
				TemplateNameLookupKind::Ordinary,
				false);
		TemplateNameLookupResult ordinary_template_lookup =
			gTemplateRegistry.lookupTemplateName(ordinary_template_lookup_request);

		if (ordinary_template_lookup.hasVariableTemplate()) {
			return { SimpleTemplateArgKind::ValueLike, std::nullopt };
		}

		// Static data members of the enclosing class being parsed (e.g. partial
		// specializations whose member function bodies are parsed eagerly) are
		// not yet in the symbol table.  Mirror the static-member lookup that
		// `parse_primary_expression` performs against the struct/member-function
		// parsing contexts so dependent NTTP arguments like
		// `is_lock_free<sizeof(_Tp), required_alignment>()` classify as
		// ValueLike instead of Unknown.
		auto isStaticDataMemberOfEnclosingClass = [&](StringHandle handle) -> bool {
			auto checkStruct = [&](const StructTypeInfo* info) -> bool {
				if (info == nullptr) {
					return false;
				}
				for (const auto& static_member : info->static_members) {
					if (static_member.getName() == handle) {
						return true;
					}
				}
				return info->findStaticMemberRecursive(handle).first != nullptr;
			};
			auto checkStructNode = [&](const StructDeclarationNode* node) -> bool {
				if (node == nullptr) {
					return false;
				}
				for (const auto& static_member : node->static_members()) {
					if (static_member.name == handle) {
						return true;
					}
				}
				return false;
			};
			auto checkByTypeIndex = [&](TypeIndex idx) -> bool {
				if (const TypeInfo* type_info = tryGetTypeInfo(idx)) {
					return checkStruct(type_info->getStructInfo());
				}
				return false;
			};
			for (auto it = struct_parsing_context_stack_.rbegin();
				 it != struct_parsing_context_stack_.rend(); ++it) {
				if (checkStructNode(it->struct_node) || checkStruct(it->local_struct_info)) {
					return true;
				}
			}
			for (auto it = member_function_context_stack_.rbegin();
				 it != member_function_context_stack_.rend(); ++it) {
				if (checkStructNode(it->struct_node) || checkStruct(it->local_struct_info)) {
					return true;
				}
				if (checkByTypeIndex(it->struct_type_index)) {
					return true;
				}
			}
			return false;
		};
		if (isStaticDataMemberOfEnclosingClass(name_handle)) {
			return { SimpleTemplateArgKind::ValueLike, std::nullopt };
		}

		if (ordinary_template_lookup.hasAliasTemplate() ||
			ordinary_template_lookup.hasClassTemplate() ||
			findTypeByName(name_handle) != nullptr) {
			return { SimpleTemplateArgKind::TypeLike, std::nullopt };
		}

		return { SimpleTemplateArgKind::Unknown, std::nullopt };
	};

	auto classifySimpleTemplateArg = [&](const ExpressionNode& expr) {
		if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&expr)) {
			return classifySimpleTemplateArgName(tparam_ref->param_name()).first;
		}
		if (const auto* id = std::get_if<IdentifierNode>(&expr)) {
			return classifySimpleTemplateArgName(StringTable::getOrInternStringHandle(id->name())).first;
		}
		return SimpleTemplateArgKind::NotSimple;
	};

	auto dependentIdentifierValueCategory = [&](StringHandle name_handle) -> std::optional<TypeCategory> {
		for (const auto& subst : template_param_substitutions_) {
			if (subst.param_name == name_handle && subst.is_value_param) {
				return subst.value_type;
			}
		}

		if (auto current_param_category = currentTemplateParamNonTypeCategory(name_handle);
			current_param_category.has_value()) {
			return current_param_category;
		}

		if (auto symbol_lookup = lookup_symbol_with_template_check(name_handle);
			symbol_lookup.has_value()) {
			if (symbol_lookup->is<VariableDeclarationNode>()) {
				return symbol_lookup->as<VariableDeclarationNode>().declaration().type_specifier_node().type();
			}
			if (symbol_lookup->is<ExpressionNode>()) {
				const ExpressionNode& lookup_expr = symbol_lookup->as<ExpressionNode>();
				if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&lookup_expr)) {
					return currentTemplateParamNonTypeCategory(tparam_ref->param_name());
				}
			}
		}

		return std::nullopt;
	};

	auto canonicalAliasTargetCategory = [](const TemplateAliasNode& alias_node) {
		const ResolvedAliasTypeInfo resolved_target_type =
			resolveAliasTypeInfo(alias_node.target_type_node().type_index());
		TypeCategory target_type = resolved_target_type.typeEnum();
		if (target_type == TypeCategory::Invalid) {
			target_type = alias_node.target_type_node().type();
		}
		return target_type;
	};

	auto dependentExpressionValueCategory =
		[&](const ExpressionNode& expr, std::optional<StringHandle> dependent_name = std::nullopt) {
			if (dependent_name.has_value()) {
				if (auto category = dependentIdentifierValueCategory(*dependent_name);
					category.has_value()) {
					return *category;
				}
			}

			if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
				const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
				if (auto category = dependentIdentifierValueCategory(tparam_ref.param_name());
					category.has_value()) {
					return *category;
				}
			}

			if (std::holds_alternative<IdentifierNode>(expr)) {
				const auto& id = std::get<IdentifierNode>(expr);
				if (auto category = dependentIdentifierValueCategory(
						StringTable::getOrInternStringHandle(id.name()));
					category.has_value()) {
					return *category;
				}
			}

			if (std::holds_alternative<SizeofExprNode>(expr) ||
				std::holds_alternative<AlignofExprNode>(expr) ||
				std::holds_alternative<OffsetofExprNode>(expr)) {
				return (g_target_data_model == TargetDataModel::LLP64)
					? TypeCategory::UnsignedLongLong
					: TypeCategory::UnsignedLong;
			}

			if (std::holds_alternative<NoexceptExprNode>(expr) ||
				std::holds_alternative<TypeTraitExprNode>(expr)) {
				return TypeCategory::Bool;
			}

			if (std::holds_alternative<BinaryOperatorNode>(expr)) {
				const auto& binary = std::get<BinaryOperatorNode>(expr);
				if (isBoolResultOperator(binary.op())) {
					return TypeCategory::Bool;
				}
			}
			if (std::holds_alternative<FoldExpressionNode>(expr)) {
				const auto& fold = std::get<FoldExpressionNode>(expr);
				if (isBoolResultOperator(fold.op())) {
					return TypeCategory::Bool;
				}
			}

		return TypeCategory::Int;
	};

	auto currentTargetTemplateParam = [&]() -> const TemplateParameterNode* {
		return currentExplicitTemplateArgumentTargetParam(template_args.size());
	};

	auto currentArgRejectsValueLikeParsing = [&]() {
		const TemplateParameterNode* target_param = currentTargetTemplateParam();
		return target_param != nullptr &&
			   target_param->kind() != TemplateParameterKind::NonType;
	};

	auto qualifiedIdNamesVariableTemplate = [&](const QualifiedIdentifierNode& qi) {
		std::string_view qname = buildQualifiedNameFromHandle(qi.namespace_handle(), qi.name());
		if (gTemplateRegistry.lookupVariableTemplate(qname).has_value() ||
			gTemplateRegistry.lookupVariableTemplate(qi.nameHandle()).has_value()) {
			return true;
		}
		if (qname.find("::") == std::string_view::npos) {
			NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
			if (!current_namespace.isGlobal()) {
				StringHandle qualified_handle =
					gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, qi.nameHandle());
				if (qualified_handle.isValid() &&
					gTemplateRegistry.lookupVariableTemplate(qualified_handle).has_value()) {
					return true;
				}
			}
		}
		return false;
	};

	auto hasConcreteSubstitutionForName = [&](StringHandle name) {
		if (!name.isValid()) {
			return false;
		}
		for (const auto& subst : template_param_substitutions_) {
			if (subst.param_name == name) {
				return true;
			}
		}
		return false;
	};

	auto expressionHasUnsubstitutedDependency =
		[&](const auto& self, const ASTNode& node) -> bool {
			if (node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();
				return typeSpecStillUsesDependentPlaceholder(type_spec);
			}
			if (!node.is<ExpressionNode>()) {
				return false;
			}
			const ExpressionNode& dep_expr = node.as<ExpressionNode>();
			if (const auto* tparam_ref =
					std::get_if<TemplateParameterReferenceNode>(&dep_expr)) {
				return !hasConcreteSubstitutionForName(tparam_ref->param_name());
			}
			if (const auto* id = std::get_if<IdentifierNode>(&dep_expr)) {
				StringHandle name = StringTable::getOrInternStringHandle(id->name());
				return currentTemplateParamKind(name).has_value() &&
					   !hasConcreteSubstitutionForName(name);
			}
			if (const auto* qual_id = std::get_if<QualifiedIdentifierNode>(&dep_expr)) {
				auto storedArgsContainUnsubstitutedDependency =
					[&](std::span<const TypeInfo::TemplateArgInfo> stored_args) {
					for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
						TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
						if (arg.dependent_name.isValid() &&
							!hasConcreteSubstitutionForName(arg.dependent_name)) {
							return true;
						}
						if (arg.dependent_expr.has_value() &&
							self(self, *arg.dependent_expr)) {
							return true;
						}
						if (arg.is_dependent && !arg.dependent_name.isValid()) {
							return true;
						}
					}
					return false;
				};
				if (const TypeInfo::DependentQualifiedNameRecord* record =
						qual_id->dependentQualifiedName()) {
					if (record->owner_name.isValid() &&
						currentTemplateParamKind(record->owner_name).has_value() &&
						!hasConcreteSubstitutionForName(record->owner_name)) {
						return true;
					}
					if (storedArgsContainUnsubstitutedDependency(
							record->owner_template_arguments)) {
						return true;
					}
					for (const auto& member : record->member_chain) {
						if (storedArgsContainUnsubstitutedDependency(
								member.template_arguments)) {
							return true;
						}
					}
				}
				std::string_view owner_name =
					gNamespaceRegistry.getQualifiedName(qual_id->namespace_handle());
				if (!owner_name.empty()) {
					auto owner_it = getTypesByNameMap().find(
						StringTable::getOrInternStringHandle(owner_name));
					if (owner_it != getTypesByNameMap().end() &&
						owner_it->second != nullptr) {
						const TypeInfo* owner_info = owner_it->second;
						if (owner_info->isDependentPlaceholder() ||
							owner_info->is_incomplete_instantiation_ ||
							owner_info->hasDependentQualifiedName()) {
							if (storedArgsContainUnsubstitutedDependency(
									owner_info->templateArgs())) {
								return true;
							}
						}
					}
				}
				return false;
			}
			if (const auto* binary = std::get_if<BinaryOperatorNode>(&dep_expr)) {
				return self(self, binary->get_lhs()) || self(self, binary->get_rhs());
			}
			if (const auto* unary = std::get_if<UnaryOperatorNode>(&dep_expr)) {
				return self(self, unary->get_operand());
			}
			if (const auto* member = std::get_if<MemberAccessNode>(&dep_expr)) {
				return self(self, member->object());
			}
			if (const auto* ptr_member = std::get_if<PointerToMemberAccessNode>(&dep_expr)) {
				return self(self, ptr_member->object()) || self(self, ptr_member->member_pointer());
			}
			if (const auto* array_subscript = std::get_if<ArraySubscriptNode>(&dep_expr)) {
				return self(self, array_subscript->array_expr()) || self(self, array_subscript->index_expr());
			}
			if (const auto* call_expr = std::get_if<CallExprNode>(&dep_expr)) {
				if (call_expr->has_receiver() && self(self, call_expr->receiver())) {
					return true;
				}
				for (const ASTNode& arg : call_expr->arguments()) {
					if (self(self, arg)) {
						return true;
					}
				}
				for (const ASTNode& template_arg : call_expr->template_arguments()) {
					if (self(self, template_arg)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* ctor_call = std::get_if<ConstructorCallNode>(&dep_expr)) {
				if (typeSpecStillUsesDependentPlaceholder(ctor_call->type_specifier_node())) {
					return true;
				}
				for (const ASTNode& arg : ctor_call->arguments()) {
					if (self(self, arg)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* init_list = std::get_if<InitializerListConstructionNode>(&dep_expr)) {
				if (self(self, init_list->element_type()) || self(self, init_list->target_type())) {
					return true;
				}
				for (const ASTNode& element : init_list->elements()) {
					if (self(self, element)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* noexc = std::get_if<NoexceptExprNode>(&dep_expr)) {
				return self(self, noexc->expr());
			}
			if (const auto* sizeof_expr = std::get_if<SizeofExprNode>(&dep_expr)) {
				return self(self, sizeof_expr->type_or_expr());
			}
			if (const auto* alignof_expr = std::get_if<AlignofExprNode>(&dep_expr)) {
				return self(self, alignof_expr->type_or_expr());
			}
			if (const auto* cast_expr = std::get_if<StaticCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<DynamicCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<ConstCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<ReinterpretCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* trait_expr = std::get_if<TypeTraitExprNode>(&dep_expr)) {
				if (trait_expr->has_type() && self(self, trait_expr->type_node())) {
					return true;
				}
				if (trait_expr->has_second_type() && self(self, trait_expr->second_type_node())) {
					return true;
				}
				for (const ASTNode& extra_type : trait_expr->additional_type_nodes()) {
					if (self(self, extra_type)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* ternary = std::get_if<TernaryOperatorNode>(&dep_expr)) {
				return self(self, ternary->condition()) ||
					   self(self, ternary->true_expr()) ||
					   self(self, ternary->false_expr());
			}
			if (const auto* fold = std::get_if<FoldExpressionNode>(&dep_expr)) {
				if (fold->has_complex_pack_expr() && fold->pack_expr().has_value() &&
					self(self, *fold->pack_expr())) {
					return true;
				}
				if (fold->init_expr().has_value() && self(self, *fold->init_expr())) {
					return true;
				}
				StringHandle pack_name = fold->pack_name_handle();
				if (pack_name.isValid()) {
					return currentTemplateParamKind(pack_name).has_value() &&
						   !hasConcreteSubstitutionForName(pack_name);
				}
				return false;
			}
			if (const auto* pack_expansion = std::get_if<PackExpansionExprNode>(&dep_expr)) {
				return self(self, pack_expansion->pattern());
			}
			return false;
		};

	while (true) {
		// Save position in case type parsing fails
		SaveHandle arg_saved_pos = save_token_position();
		StringHandle dependent_member_probe = extractDependentMemberProbeFromCurrentTemplateArg();

		if (peek().is_identifier()) {
			const Token identifier_token = current_token_;
			const StringHandle identifier_handle = identifier_token.handle();
			SaveHandle identifier_saved_pos = save_token_position();
			advance();

			if (peek() == "<"_tok) {
				TemplateNameLookupRequest alias_lookup_request =
					buildTemplateNameLookupRequest(
						identifier_handle,
						TemplateNameLookupKind::Ordinary,
						false);
				TemplateNameLookupResult alias_lookup =
					gTemplateRegistry.lookupTemplateName(alias_lookup_request);
				auto alias_opt = alias_lookup.firstDeclarationOfKind(
					TemplateDeclarationKind::AliasTemplate);
				if (alias_opt.has_value()) {
					const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
					TypeCategory target_type = canonicalAliasTargetCategory(alias_node);
					if (target_type == TypeCategory::Void) {
						auto alias_args_opt = parse_explicit_template_arguments(
							alias_node.template_parameters(),
							static_cast<std::vector<ASTNode>*>(nullptr));
						if (alias_args_opt.has_value()) {
							TemplateTypeArg alias_arg;
							alias_arg.setType(target_type);
							alias_arg.is_dependent = false;
							for (const TemplateTypeArg& alias_inner_arg : *alias_args_opt) {
								if (alias_inner_arg.dependent_name.isValid() &&
									StringTable::getStringView(alias_inner_arg.dependent_name).find("::") != std::string_view::npos) {
									alias_arg.dependent_name = alias_inner_arg.dependent_name;
									break;
								}
								if (const TypeInfo* alias_inner_type = tryGetTypeInfo(alias_inner_arg.type_index)) {
									std::string_view alias_inner_name = StringTable::getStringView(alias_inner_type->name());
									if (alias_inner_name.find("::") != std::string_view::npos) {
										alias_arg.dependent_name = StringTable::getOrInternStringHandle(alias_inner_name);
										break;
									}
								}
							}
							if (!alias_arg.dependent_name.isValid()) {
								alias_arg.dependent_name = dependent_member_probe;
							}

							if (peek() == "..."_tok) {
								advance();
								alias_arg.is_pack = true;
								FLASH_LOG(Templates, Debug, "Marked alias template argument as pack expansion");
							}

							template_args.push_back(alias_arg);
							discard_saved_token(identifier_saved_pos);
							discard_saved_token(arg_saved_pos);

							if (peek() == ">>"_tok) {
								split_right_shift_token();
							}
							if (peek() == ">"_tok) {
								advance();
								break;
							}
							if (peek() == ","_tok) {
								advance();
								continue;
							}

							FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after alias template argument");
							restore_token_position(saved_pos);
							last_failed_template_arg_parse_handle_ = saved_pos;
							return std::nullopt;
						}
					}
				}

				restore_token_position(identifier_saved_pos);
			}

			if (peek() == ">>"_tok) {
				split_right_shift_token();
			}

			bool is_pack_expansion = false;
			if (peek() == "..."_tok) {
				advance();
				is_pack_expansion = true;
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}
			}

			if (peek() == ">"_tok || peek() == ","_tok) {
				if (parsing_alias_type_id_ &&
					(gTemplateRegistry.lookup_alias_template(identifier_handle).has_value() ||
					 gTemplateRegistry.lookupTemplate(identifier_handle).has_value() ||
					 gTemplateRegistry.isClassTemplate(identifier_handle) ||
					 (currentTemplateParamKind(identifier_handle).has_value() &&
					  *currentTemplateParamKind(identifier_handle) == TemplateParameterKind::Template))) {
					TemplateTypeArg template_arg = TemplateTypeArg::makeTemplate(identifier_handle);
					template_arg.is_pack = is_pack_expansion;
					template_args.push_back(template_arg);
					if (out_type_nodes) {
						out_type_nodes->push_back(ASTNode::emplace_node<ExpressionNode>(IdentifierNode(identifier_token)));
					}
					discard_saved_token(identifier_saved_pos);
					discard_saved_token(arg_saved_pos);

					if (peek() == ">"_tok) {
						advance();
						break;
					}

					advance();
					continue;
				}

				auto [simple_identifier_kind, prebuilt_arg] = classifySimpleTemplateArgName(identifier_handle);
				if (simple_identifier_kind == SimpleTemplateArgKind::ValueLike &&
					!currentArgRejectsValueLikeParsing()) {
					// Use the concrete arg returned by the classify call (avoids a second loop over
					// template_param_substitutions_). Fall back to a dependent-value placeholder when
					// no concrete substitution was found yet (deferred template-body parse context).
					TemplateTypeArg simple_arg = prebuilt_arg.value_or(
						TemplateTypeArg::makeDependentValue(
							identifier_handle,
							dependentExpressionValueCategory(
								ExpressionNode(IdentifierNode(identifier_token)),
								identifier_handle),
							0,
							ASTNode::emplace_node<ExpressionNode>(IdentifierNode(identifier_token))));

					simple_arg.is_pack = is_pack_expansion;
					template_args.push_back(simple_arg);
					if (out_type_nodes) {
						out_type_nodes->push_back(ASTNode::emplace_node<ExpressionNode>(IdentifierNode(identifier_token)));
					}
					discard_saved_token(identifier_saved_pos);
					discard_saved_token(arg_saved_pos);

					if (peek() == ">"_tok) {
						advance();
						break;
					}

					advance();
					continue;
				}
			}

			restore_token_position(identifier_saved_pos);
		}

		// First, try to parse an expression (for non-type template parameters)
		// Use parse_expression with ExpressionContext::TemplateTypeArg to handle
		// member access expressions like is_int<T>::value and complex expressions
		// like T::value || my_or<Rest...>::value
		// Precedence 2 allows all binary operators except comma (precedence 1)
		// The TemplateTypeArg context ensures we stop at '>' and ',' delimiters
		if (!currentArgRejectsValueLikeParsing()) {
			auto expr_result = parse_expression(2, ExpressionContext::TemplateTypeArg);
			if (!expr_result.is_error() && expr_result.node().has_value()) {
			// Successfully parsed an expression - check if it's a boolean or numeric literal
			const ExpressionNode& expr = expr_result.node()->as<ExpressionNode>();

			// Handle boolean literals (true/false)
			if (std::holds_alternative<BoolLiteralNode>(expr)) {
				const BoolLiteralNode& lit = std::get<BoolLiteralNode>(expr);
				TemplateTypeArg bool_arg(lit.value() ? 1 : 0, TypeCategory::Bool);

				// Check for pack expansion (...)
				if (peek() == "..."_tok) {
					advance(); // consume '...'
					bool_arg.is_pack = true;
					FLASH_LOG(Templates, Debug, "Marked boolean literal as pack expansion");
				}

				template_args.push_back(bool_arg);
				if (out_type_nodes && expr_result.node().has_value()) {
					out_type_nodes->push_back(*expr_result.node());
				}
				discard_saved_token(arg_saved_pos);

				// Check for ',' or '>' after the boolean literal (or after pack expansion)
				if (peek().is_eof()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Phase 5: Handle >> token splitting for nested templates
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}

				if (peek() == ">"_tok) {
					advance(); // consume '>'
					break;
				}

				if (peek() == ","_tok) {
					advance(); // consume ','
					continue;
				}

				// Unexpected token after boolean literal
				FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after boolean literal");
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}

			// Handle numeric literals
			if (std::holds_alternative<NumericLiteralNode>(expr)) {
				const NumericLiteralNode& lit = std::get<NumericLiteralNode>(expr);
				const auto& val = lit.value();
				TypeCategory literal_type = lit.type();	// Get the type of the literal (bool, int, etc.)
				TemplateTypeArg num_arg;
				if (const auto* ull_val = std::get_if<unsigned long long>(&val)) {
					num_arg = TemplateTypeArg(static_cast<int64_t>(*ull_val), literal_type);
					if (literal_type == TypeCategory::Nullptr) {
						num_arg.setValueIdentity(FlashCpp::NonTypeValueIdentity::makeNullptr(nativeTypeIndex(TypeCategory::Nullptr)));
					}
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>' or '...'
				} else if (const auto* d_val = std::get_if<double>(&val)) {
					num_arg = TemplateTypeArg(static_cast<int64_t>(*d_val), literal_type);
					discard_saved_token(arg_saved_pos);
					// Successfully parsed a non-type template argument, continue to check for ',' or '>' or '...'
				} else {
					FLASH_LOG(Parser, Error, "Unsupported numeric literal type");
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Check for pack expansion (...)
				if (peek() == "..."_tok) {
					advance(); // consume '...'
					num_arg.is_pack = true;
					FLASH_LOG(Templates, Debug, "Marked numeric literal as pack expansion");
				}

				template_args.push_back(num_arg);
				if (out_type_nodes && expr_result.node().has_value()) {
					out_type_nodes->push_back(*expr_result.node());
				}

				// Check for ',' or '>' after the numeric literal (or after pack expansion)
				if (peek().is_eof()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Phase 5: Handle >> token splitting for nested templates
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}

				if (peek() == ">"_tok) {
					advance(); // consume '>'
					break;
				}

				if (peek() == ","_tok) {
					advance(); // consume ','
					continue;
				}

				// Unexpected token after numeric literal
				FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after numeric literal: '",
						  peek_info().value(), "' (might be comparison operator)");
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}

			// Expression is not a numeric literal - try to evaluate it as a constant expression
			// This handles cases like is_int<T>::value where the expression needs evaluation
			// Always attempt constant evaluation first. Non-dependent expressions such as
			// pointer/function-pointer NTTPs in explicit specializations must materialize to
			// concrete identities even when parsed inside template declarations. Dependent
			// expressions naturally fall through when evaluation fails.
			bool should_try_constant_eval =
				!expressionHasUnsubstitutedDependency(
					expressionHasUnsubstitutedDependency,
					*expr_result.node());
			if (should_try_constant_eval) {
				FLASH_LOG(
					Templates,
					Debug,
					"Trying to evaluate non-literal expression as constant (sfinae_probe=",
					template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe,
					", parsing_template_body=",
					parsing_template_depth_ > 0,
					")");
				auto const_value = try_evaluate_constant_expression(*expr_result.node());
				if (const_value.has_value()) {
					// Successfully evaluated as a constant expression.
					// Preserve full typed NTTP identity (pointer/reference/member/nullptr/etc)
					// instead of collapsing to raw integral payload.
					TemplateTypeArg const_arg = TemplateTypeArg::makeValueIdentity(const_value->identity);

					// Check for pack expansion (...)
					if (peek() == "..."_tok) {
						advance(); // consume '...'
						const_arg.is_pack = true;
						FLASH_LOG(Templates, Debug, "Marked constant expression as pack expansion");
					}

					template_args.push_back(const_arg);
					if (out_type_nodes && expr_result.node().has_value()) {
						out_type_nodes->push_back(*expr_result.node());
					}
					discard_saved_token(arg_saved_pos);

					// Check for ',' or '>' after the expression (or after pack expansion)
					if (peek().is_eof()) {
						restore_token_position(saved_pos);
						last_failed_template_arg_parse_handle_ = saved_pos;
						return std::nullopt;
					}

					// Phase 5: Handle >> token splitting for nested templates
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}

					if (peek() == ">"_tok) {
						advance(); // consume '>'
						break;
					}

					if (peek() == ","_tok) {
						advance(); // consume ','
						continue;
					}

					// Unexpected token after expression
					FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token after constant expression");
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				// Constant evaluation failed - check if this is a noexcept or similar expression
				// that should be accepted as a dependent template argument.
				// NoexceptExprNode, SizeofExprNode, AlignofExprNode, and TypeTraitExprNode are
				// compile-time expressions that may contain dependent expressions.
				// QualifiedIdentifierNode represents patterns like is_same<T, int>::value where
				// the expression is a static member access that depends on template parameters.
				// If the next token is a valid delimiter, accept the expression as dependent.
				auto isDependentCompileTimeExpr = [&](const ExpressionNode& candidate) {
					return std::holds_alternative<NoexceptExprNode>(candidate) ||
						   std::holds_alternative<SizeofExprNode>(candidate) ||
						   std::holds_alternative<SizeofPackNode>(candidate) ||
						   std::holds_alternative<AlignofExprNode>(candidate) ||
						   std::holds_alternative<OffsetofExprNode>(candidate) ||
						   std::holds_alternative<TypeTraitExprNode>(candidate) ||
						   std::holds_alternative<MemberAccessNode>(candidate) ||
						   std::holds_alternative<QualifiedIdentifierNode>(candidate) ||
						   std::holds_alternative<BinaryOperatorNode>(candidate) ||
						   std::holds_alternative<TernaryOperatorNode>(candidate) ||
						   std::holds_alternative<UnaryOperatorNode>(candidate) ||
						   std::holds_alternative<StaticCastNode>(candidate) ||
						   std::holds_alternative<CallExprNode>(candidate) ||
						   std::holds_alternative<ConstructorCallNode>(candidate) ||
						   std::holds_alternative<FoldExpressionNode>(candidate) ||
						   std::holds_alternative<PackExpansionExprNode>(candidate);
				};
				bool is_compile_time_expr = isDependentCompileTimeExpr(expr);

				if (is_compile_time_expr && !peek().is_eof()) {
					// Handle >> token splitting for nested templates
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}

					// Before accepting as dependent, check if a QualifiedIdentifierNode is actually
					// a concrete type (e.g. std::ratio<1,2> which was already instantiated during
					// expression parsing). Concrete types should fall through to type parsing,
					// not be marked as dependent compile-time expressions.
					bool is_concrete_qualified_type = false;
					if (std::holds_alternative<QualifiedIdentifierNode>(expr) &&
						(peek() == ">"_tok || peek() == ","_tok)) {
						const auto& qi = std::get<QualifiedIdentifierNode>(expr);
						std::string_view qname = buildQualifiedNameFromHandle(qi.namespace_handle(), qi.name());
						const bool variable_template_expression =
							qi.has_template_arguments() && qualifiedIdNamesVariableTemplate(qi);
						auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(qname));
						if (!variable_template_expression &&
							type_it != getTypesByNameMap().end() &&
							type_it->second != nullptr &&
							(type_it->second->isTypeAlias() ||
							 type_it->second->getStructInfo() != nullptr ||
							 type_it->second->getEnumInfo() != nullptr ||
							 type_it->second->isTemplateInstantiation() ||
							 type_it->second->isDependentMemberType())) {
							FLASH_LOG(Templates, Debug, "QualifiedIdentifierNode '", qname,
									  "' is a concrete type, falling through to type parsing");
							is_concrete_qualified_type = true;
							restore_token_position(arg_saved_pos);
						}
						if (!is_concrete_qualified_type && qi.has_template_arguments()) {
							bool names_alias_template = false;
							if (gTemplateRegistry.lookup_alias_template(qname).has_value()) {
								names_alias_template = true;
							} else if (const size_t scope_pos = qname.rfind("::");
									   scope_pos != std::string_view::npos) {
								const std::string_view owner_name = qname.substr(0, scope_pos);
								const std::string_view member_name = qname.substr(scope_pos + 2);
								std::string_view inherited_member_template_name;
								if (!member_name.empty()) {
									inherited_member_template_name =
										lookup_inherited_member_template_name(
											StringTable::getOrInternStringHandle(owner_name),
											StringTable::getOrInternStringHandle(member_name),
											0);
								}
								if (!inherited_member_template_name.empty() &&
									gTemplateRegistry.lookup_alias_template(
										inherited_member_template_name)
										.has_value()) {
									names_alias_template = true;
								}
							}
							if (!names_alias_template &&
								gTemplateRegistry.lookup_alias_template(qi.nameHandle()).has_value()) {
								names_alias_template = true;
							}
							if (names_alias_template) {
								FLASH_LOG(Templates, Debug, "QualifiedIdentifierNode '", qname,
										  "' names an alias template, falling through to type parsing");
								is_concrete_qualified_type = true;
								restore_token_position(arg_saved_pos);
							}
						}
					}

					if (!is_concrete_qualified_type &&
						!currentArgRejectsValueLikeParsing() &&
						(peek() == ">"_tok || peek() == ","_tok || peek() == "..."_tok)) {
						FLASH_LOG(Templates, Debug, "Accepting dependent compile-time expression as template argument");
						// Create a dependent template argument
						auto makeDependentCompileTimeArg = [&](StringHandle dependent_name, std::optional<ASTNode> dep_expr) {
							TypeCategory value_category = dependentExpressionValueCategory(
								expr,
								dependent_name.isValid()
								? std::optional<StringHandle>(dependent_name)
								: std::nullopt);
							return TemplateTypeArg::makeDependentValue(dependent_name, value_category, 0, std::move(dep_expr));
						};

						// For compile-time expressions like sizeof(T), alignof(T), store the AST expression
						// so it can be re-evaluated during template instantiation with concrete arguments.
						std::optional<ASTNode> stored_expr = std::nullopt;
						if (std::holds_alternative<IdentifierNode>(expr) ||
							std::holds_alternative<TemplateParameterReferenceNode>(expr) ||
							isDependentCompileTimeExpr(expr)) {
							if (expr_result.node().has_value()) {
								stored_expr = *expr_result.node();
							}
						}

						TemplateTypeArg dependent_arg = makeDependentCompileTimeArg(StringHandle{}, stored_expr);
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							dependent_arg = makeDependentCompileTimeArg(
								StringTable::getOrInternStringHandle(id.name()), std::nullopt);
						} else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
							const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
							dependent_arg = makeDependentCompileTimeArg(
								StringTable::getOrInternStringHandle(qual_id.full_name()), std::nullopt);
						}

						// Check for pack expansion (...)
						if (peek() == "..."_tok) {
							advance(); // consume '...'
							dependent_arg.is_pack = true;
							FLASH_LOG(Templates, Debug, "Marked compile-time expression as pack expansion");
						}
						template_args.push_back(dependent_arg);
						if (out_type_nodes && expr_result.node().has_value()) {
							out_type_nodes->push_back(*expr_result.node());
						}
						discard_saved_token(arg_saved_pos);

						// Handle >> token splitting again after pack expansion check
						if (peek() == ">>"_tok) {
							split_right_shift_token();
						}

						if (peek() == ">"_tok) {
							advance(); // consume '>'
							break;
						}

						if (peek() == ","_tok) {
							advance(); // consume ','
							continue;
						}
					}
				}
			} else {
				FLASH_LOG(Templates, Debug, "Skipping constant expression evaluation (in template body with dependent context)");

				// BUGFIX: Even in a template body, static constexpr members like __g and __d2
				// in a partial specialization have concrete values and should be evaluated.
				// Try constant evaluation for simple identifiers that refer to static members.
				bool evaluated_static_member = false;
				std::optional<ConstantValue> static_member_value;

				if (std::holds_alternative<IdentifierNode>(expr) && !struct_parsing_context_stack_.empty()) {
					const auto& id = std::get<IdentifierNode>(expr);
					StringHandle id_handle = StringTable::getOrInternStringHandle(id.name());
					const auto& ctx = struct_parsing_context_stack_.back();

					// Check local_struct_info for static constexpr members
					if (ctx.local_struct_info != nullptr) {
						for (const auto& static_member : ctx.local_struct_info->static_members) {
							if (static_member.getName() == id_handle && static_member.initializer.has_value()) {
								// Try to evaluate the static member's initializer
								static_member_value = try_evaluate_constant_expression(*static_member.initializer);
								if (static_member_value.has_value()) {
									FLASH_LOG(Templates, Debug, "Evaluated static constexpr member '", id.name(),
											  "' to value ", static_member_value->value);
									evaluated_static_member = true;
								}
								break;
							}
						}
					}

					// Also check struct_node's static_members
					if (!evaluated_static_member && ctx.struct_node != nullptr) {
						for (const auto& static_member : ctx.struct_node->static_members()) {
							if (static_member.name == id_handle && static_member.initializer.has_value()) {
								static_member_value = try_evaluate_constant_expression(*static_member.initializer);
								if (static_member_value.has_value()) {
									FLASH_LOG(Templates, Debug, "Evaluated static constexpr member '", id.name(),
											  "' (from struct_node) to value ", static_member_value->value);
									evaluated_static_member = true;
								}
								break;
							}
						}
					}
				}

				if (evaluated_static_member && static_member_value.has_value()) {
					// Successfully evaluated static member - preserve full typed NTTP identity.
					TemplateTypeArg const_arg = TemplateTypeArg::makeValueIdentity(static_member_value->identity);

					// Check for pack expansion (...)
					if (peek() == "..."_tok) {
						advance();
						const_arg.is_pack = true;
					}

					template_args.push_back(const_arg);
					discard_saved_token(arg_saved_pos);

					// Handle next token
					if (peek() == ">>"_tok) {
						split_right_shift_token();
					}
					if (peek() == ">"_tok) {
						advance();
						break;  // Break from outer while loop
					}
					if (peek() == ","_tok) {
						advance();
						continue;  // Continue to next template argument
					}
				}

				// During template declaration, expressions like is_int<T>::value are dependent
				// and cannot be evaluated yet. Check if we successfully parsed such an expression
				// by verifying that the next token is ',' or '>'
				FLASH_LOG_FORMAT(Templates, Debug, "After parsing expression, peek_token={}",
								 !peek().is_eof() ? std::string(peek_info().value()) : "N/A");

				// Special case: If we parsed T[N] as an array subscript expression,
				// this is actually an array type declarator in a specialization pattern,
				// not an array access. Reparse as a type.
				bool is_array_subscript = std::holds_alternative<ArraySubscriptNode>(expr);
				if (is_array_subscript) {
					FLASH_LOG(Templates, Debug, "Detected array subscript in template arg - reparsing as array type");
					restore_token_position(arg_saved_pos);
					// Fall through to type parsing below
				} else {

					// Special case: If out_type_nodes is provided AND the expression is a simple identifier,
					// we should fall through to type parsing so identifiers get properly converted to TypeSpecifierNode.
					// This is needed for deduction guides where template parameters must be TypeSpecifierNode.
					// However, complex expressions like is_int<T>::value should still be accepted as dependent expressions.
					//
					// ALSO: If we parsed a simple identifier followed by '<', we should fall through to type parsing
					// because this is likely a template type (e.g., enable_if_t<...>), not a value expression.
					//
					// ALSO: If followed by '[', this is an array type declarator - must parse as type
					//
					// IMPORTANT: If followed by '...', this is pack expansion, NOT a type - accept as dependent expression
					bool is_simple_identifier = std::holds_alternative<IdentifierNode>(expr) ||
												std::holds_alternative<TemplateParameterReferenceNode>(expr);
					SimpleTemplateArgKind simple_identifier_kind = classifySimpleTemplateArg(expr);
					bool simple_identifier_is_value_like = simple_identifier_kind == SimpleTemplateArgKind::ValueLike;
					// Keep Unknown identifiers potentially type-like here for compatibility
					// with existing deduction-guide / type-reparse paths. Bare unknown
					// identifier NTTPs that are immediately followed by a delimiter are
					// handled earlier by the direct-identifier fast path above.
					bool simple_identifier_can_be_type_like = is_simple_identifier && !simple_identifier_is_value_like;
					[[maybe_unused]] bool is_function_call_expr = CallInfo::tryFrom(expr).has_value();
					bool followed_by_template_args = peek() == "<"_tok;
					bool followed_by_array_declarator = peek() == "["_tok;
					bool followed_by_pack_expansion = peek() == "..."_tok;
					bool followed_by_reference = !peek().is_eof() && (peek() == "&"_tok || peek() == "&&"_tok);
					bool followed_by_pointer = peek() == "*"_tok;
					bool should_try_type_parsing = (out_type_nodes != nullptr && simple_identifier_can_be_type_like &&
													!followed_by_pack_expansion) ||
												   (simple_identifier_can_be_type_like && followed_by_template_args) ||
												   (simple_identifier_can_be_type_like && followed_by_array_declarator) ||
												   (simple_identifier_can_be_type_like && followed_by_reference) ||
												   (simple_identifier_can_be_type_like && followed_by_pointer);

					if (!should_try_type_parsing && !peek().is_eof() &&
						(peek() == ","_tok || peek() == ">"_tok || peek() == ">>"_tok || peek() == "..."_tok)) {
						// Check if this is actually a concrete type (not a template parameter)
						// If it's a concrete struct or type alias, we should fall through to type parsing instead
						bool is_concrete_type = false;
						if (std::holds_alternative<IdentifierNode>(expr)) {
							const auto& id = std::get<IdentifierNode>(expr);
							auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(id.name()));
							if (type_it != getTypesByNameMap().end()) {
								const TypeInfo* type_info = type_it->second;
								// Check if it's a concrete struct (has struct_info_)
								// OR if it's a type alias that resolves to a concrete type
								// Type aliases have type_index pointing to the underlying type
								if (type_info->struct_info_ != nullptr) {
									is_concrete_type = true;
									FLASH_LOG(Templates, Debug, "Identifier '", id.name(), "' is a concrete struct type, falling through to type parsing");
								} else if (const TypeInfo* underlying = tryGetTypeInfo(type_info->type_index_)) {
									// Check if this is a type alias (type_index points to underlying type)
									// and the underlying type is concrete (not a template parameter)
									// A type is concrete if:
									// 1. It has struct_info_ (it's a defined struct/class), OR
									// 2. It's not Type::UserDefined (i.e., it's a built-in type like int, bool, float)
									// Template parameters are stored as Type::UserDefined without struct_info_,
									// so this check correctly excludes them while accepting concrete types.
									if (underlying->struct_info_ != nullptr ||
										underlying->resolvedType() != TypeCategory::UserDefined) {
									// It's a type alias to a concrete type (struct or built-in)
										is_concrete_type = true;
										FLASH_LOG(Templates, Debug, "Identifier '", id.name(), "' is a type alias to concrete type, falling through to type parsing");
									}
								}
							}
						} else if (CallInfo::tryFrom(expr).has_value()) {
							// Call expressions represent a function call expression like test_func<T>()
							// This is NOT a type - it's a non-type template argument (the result of calling a function)
							// Previously this code incorrectly treated call expressions with template arguments as a type,
							// but that was wrong. A function call with template arguments (e.g., test_func<T>()) is still
							// a function call, not a type. The function returns a value, and that value is used as
							// the non-type template argument.
							// DO NOT set is_concrete_type = true here - let it be accepted as a dependent expression.
							FLASH_LOG(Templates, Debug, "Call expression - treating as function call expression, not a type");
						} else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
							// QualifiedIdentifierNode can represent a namespace-qualified type like ns::Inner
							// or a template instantiation like ns::Inner<int> (when the template has already been
							// instantiated during expression parsing).
							const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
							// Build the qualified name and check if it exists in getTypesByNameMap()
							std::string_view qualified_name = buildQualifiedNameFromHandle(qual_id.namespace_handle(), qual_id.name());
							const bool variable_template_expression =
								qual_id.has_template_arguments() && qualifiedIdNamesVariableTemplate(qual_id);
							auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(qualified_name));
							if (!variable_template_expression &&
								type_it != getTypesByNameMap().end()) {
								const TypeInfo* type_info = type_it->second;
								if (type_info->struct_info_ != nullptr) {
									is_concrete_type = true;
									FLASH_LOG(Templates, Debug, "QualifiedIdentifierNode '", qualified_name, "' is a concrete type, falling through to type parsing");
								}
							}
							// Keep unresolved qualified-ids value-like by default.
							// Whether `A::B` is a type-id or a non-type expression is
							// context-sensitive and must be decided against the target
							// template-parameter kind instead of template-name heuristics.
							// Forcing unresolved `A::B` to type-like here regresses
							// non-type contexts such as ValueSlot<Owner::value>.
						}

						// If it's a concrete type, restore and let type parsing handle it
						if (is_concrete_type) {
							restore_token_position(arg_saved_pos);
							// Fall through to type parsing below
						} else {
							// Check if this is a template parameter that has a type substitution available
							// This enables variable templates inside function templates to work correctly:
							// e.g., __is_ratio_v<_R1> where _R1 should be substituted with ratio<1,2>
							bool substituted_type_param = false;
							bool substituted_value_param = false;
							bool finished_parsing = false;  // Track if we consumed '>' and should break
							std::string_view param_name_to_check;

							if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&expr)) {
								param_name_to_check = StringTable::getStringView(tparam_ref->param_name());
							} else if (const auto* id = std::get_if<IdentifierNode>(&expr)) {
								param_name_to_check = id->name();
							}

							if (!param_name_to_check.empty()) {
								// Check if we have a type substitution for this parameter
								for (const auto& subst : template_param_substitutions_) {
									if (subst.is_type_param && subst.param_name == param_name_to_check) {
										// Found a type substitution! Use it instead of creating a dependent arg
										FLASH_LOG(Templates, Debug, "Found type substitution for parameter '",
												  param_name_to_check, "' -> ", subst.substituted_type.toString());

										TemplateTypeArg substituted_arg = subst.substituted_type;

										// Check for pack expansion (...)
										if (peek() == "..."_tok) {
											advance(); // consume '...'
											substituted_arg.is_pack = true;
											FLASH_LOG(Templates, Debug, "Marked substituted type as pack expansion");
										}

										template_args.push_back(substituted_arg);
										if (out_type_nodes && expr_result.node().has_value()) {
											out_type_nodes->push_back(*expr_result.node());
										}
										discard_saved_token(arg_saved_pos);
										substituted_type_param = true;

										// Handle next token
										if (peek() == ">>"_tok) {
											split_right_shift_token();
										}
										if (peek() == ">"_tok) {
											advance();
											finished_parsing = true;
										} else if (peek() == ","_tok) {
											advance();
										}
										break;  // Break from the for loop
									} else if (subst.is_value_param && subst.param_name == param_name_to_check) {
										FLASH_LOG(Templates, Debug, "Found value substitution for parameter '",
												  param_name_to_check, "' -> ", subst.value);

										TemplateTypeArg substituted_arg = TemplateTypeArg::makeValue(
											subst.value,
											subst.value_type);

										if (peek() == "..."_tok) {
											advance(); // consume '...'
											substituted_arg.is_pack = true;
											FLASH_LOG(Templates, Debug, "Marked substituted value as pack expansion");
										}

										template_args.push_back(substituted_arg);
										discard_saved_token(arg_saved_pos);
										substituted_value_param = true;

										if (peek() == ">>"_tok) {
											split_right_shift_token();
										}
										if (peek() == ">"_tok) {
											advance();
											finished_parsing = true;
										} else if (peek() == ","_tok) {
											advance();
										}
										break;
									}
								}
							}

							if (substituted_type_param || substituted_value_param) {
								if (finished_parsing) {
									break;  // Break from the outer while loop - we're done
								}
								continue;  // Continue to next template argument
							}

							FLASH_LOG(Templates, Debug, "Accepting dependent expression as template argument");
							// Successfully parsed a dependent expression
							// Create a dependent template argument
							// IMPORTANT: Preserve whether this is a type-like placeholder (e.g. T)
							// or a value-like dependent expression (e.g. Trait<T>::value).
							// Try to get the type_index for the template parameter so pattern matching can detect reused parameters.
							TemplateTypeArg dependent_arg;
							bool is_value_like_dependent_expr =
								std::holds_alternative<QualifiedIdentifierNode>(expr) ||
								std::holds_alternative<MemberAccessNode>(expr) ||
								std::holds_alternative<NoexceptExprNode>(expr) ||
								std::holds_alternative<SizeofExprNode>(expr) ||
								std::holds_alternative<SizeofPackNode>(expr) ||
								std::holds_alternative<AlignofExprNode>(expr) ||
								std::holds_alternative<OffsetofExprNode>(expr) ||
								std::holds_alternative<TypeTraitExprNode>(expr) ||
								std::holds_alternative<BinaryOperatorNode>(expr) ||
								std::holds_alternative<TernaryOperatorNode>(expr) ||
								std::holds_alternative<UnaryOperatorNode>(expr) ||
								std::holds_alternative<StaticCastNode>(expr) ||
								std::holds_alternative<CallExprNode>(expr) ||
								std::holds_alternative<ConstructorCallNode>(expr) ||
								simple_identifier_kind == SimpleTemplateArgKind::ValueLike;
							if (is_value_like_dependent_expr &&
								!currentArgRejectsValueLikeParsing()) {
								// Store the original AST expression for dependent NTTP expressions
								// so it can be re-evaluated during template instantiation
								std::optional<ASTNode> stored_expr = std::nullopt;
								if ((std::holds_alternative<IdentifierNode>(expr) ||
								 std::holds_alternative<TemplateParameterReferenceNode>(expr) ||
								 std::holds_alternative<QualifiedIdentifierNode>(expr) ||
								 std::holds_alternative<MemberAccessNode>(expr) ||
								 std::holds_alternative<SizeofExprNode>(expr) ||
								 std::holds_alternative<SizeofPackNode>(expr) ||
								 std::holds_alternative<AlignofExprNode>(expr) ||
								 std::holds_alternative<OffsetofExprNode>(expr) ||
								 std::holds_alternative<NoexceptExprNode>(expr) ||
								 std::holds_alternative<TypeTraitExprNode>(expr) ||
									 std::holds_alternative<BinaryOperatorNode>(expr) ||
									 std::holds_alternative<TernaryOperatorNode>(expr) ||
									 std::holds_alternative<UnaryOperatorNode>(expr) ||
									 std::holds_alternative<StaticCastNode>(expr) ||
									 std::holds_alternative<CallExprNode>(expr) ||
									 std::holds_alternative<ConstructorCallNode>(expr)) &&
									expr_result.node().has_value()) {
									stored_expr = *expr_result.node();
									FLASH_LOG(Templates, Debug, "Storing dependent NTTP expression (sizeof/alignof/etc) for re-evaluation");
								}
								dependent_arg = TemplateTypeArg::makeDependentValue(
									StringHandle{},
									dependentExpressionValueCategory(expr),
									0,
									std::move(stored_expr));
							} else if (!is_value_like_dependent_expr) {
								dependent_arg.type_index = nativeTypeIndex(TypeCategory::UserDefined);  // Template parameter is a user-defined type placeholder; will try to look up
								dependent_arg.is_value = false;	// This is a TYPE parameter, not a value
							} else {
								goto try_type_template_argument_parse;
							}
							dependent_arg.is_dependent = true;

							// Try to get the type_index for template parameter references
							// For TemplateParameterReferenceNode or IdentifierNode that refers to a template parameter
							if (std::holds_alternative<TemplateParameterReferenceNode>(expr)) {
								const auto& tparam_ref = std::get<TemplateParameterReferenceNode>(expr);
								StringHandle param_name = tparam_ref.param_name();
								// Store the dependent name for placeholder type generation
								dependent_arg.dependent_name = param_name;
								// Look up the template parameter type in getTypesByNameMap()
								auto type_it = getTypesByNameMap().find(param_name);
								if (type_it != getTypesByNameMap().end()) {
									dependent_arg.type_index = type_it->second->type_index_;
									FLASH_LOG(Templates, Debug, "  Found type_index=", dependent_arg.type_index,
											  " for template parameter '", StringTable::getStringView(param_name), "'");
								}
							} else if (std::holds_alternative<IdentifierNode>(expr)) {
								const auto& id = std::get<IdentifierNode>(expr);
								// Store the dependent name for placeholder type generation
								dependent_arg.dependent_name = StringTable::getOrInternStringHandle(id.name());
								// Check if this identifier is a template parameter by looking it up
								auto type_it = getTypesByNameMap().find(StringTable::getOrInternStringHandle(id.name()));
								if (type_it != getTypesByNameMap().end()) {
									dependent_arg.type_index = type_it->second->type_index_;
									FLASH_LOG(Templates, Debug, "  Found type_index=", dependent_arg.type_index,
											  " for identifier '", id.name(), "'");
								} else {
									// Check if this identifier is a template alias (like void_t)
									// Template aliases may resolve to concrete types even when used with dependent arguments
									TemplateNameLookupRequest alias_lookup_request =
										buildTemplateNameLookupRequest(
											StringTable::getOrInternStringHandle(id.name()),
											TemplateNameLookupKind::Ordinary,
											false);
									TemplateNameLookupResult alias_lookup =
										gTemplateRegistry.lookupTemplateName(alias_lookup_request);
									auto alias_opt = alias_lookup.firstDeclarationOfKind(
										TemplateDeclarationKind::AliasTemplate);
									if (alias_opt.has_value()) {
										const TemplateAliasNode& alias_node = alias_opt->as<TemplateAliasNode>();
										TypeCategory target_type = canonicalAliasTargetCategory(alias_node);

										// If the alias always resolves to a concrete type (like void_t -> void),
										// use that concrete type instead of marking as dependent
										if (target_type == TypeCategory::Void) {
											FLASH_LOG(Templates, Debug, "Template alias '", id.name(),
													  "' resolves to concrete type ", static_cast<int>(target_type));
											dependent_arg.setType(target_type);
											dependent_arg.is_dependent = false;	// Not dependent - resolves to concrete type
										}
									}
								}
							} else if (std::holds_alternative<QualifiedIdentifierNode>(expr)) {
								const auto& qual_id = std::get<QualifiedIdentifierNode>(expr);
								dependent_arg.dependent_name = StringTable::getOrInternStringHandle(qual_id.full_name());
							}

							// Check for pack expansion (...)
							if (peek() == "..."_tok) {
								advance(); // consume '...'
								dependent_arg.is_pack = true;
								FLASH_LOG(Templates, Debug, "Marked dependent expression as pack expansion");
							}

							template_args.push_back(dependent_arg);

							// Store the expression node for deferred base class resolution
							// This is needed so that type trait expressions like __has_trivial_destructor(T)
							// can be properly substituted and evaluated during template instantiation
							if (out_type_nodes && expr_result.node().has_value()) {
								out_type_nodes->push_back(*expr_result.node());
							}

							discard_saved_token(arg_saved_pos);

							// Check for ',' or '>' after the expression (or after pack expansion)
							// Phase 5: Handle >> token splitting for nested templates
							if (peek() == ">>"_tok) {
								split_right_shift_token();
							}

							if (peek() == ">"_tok) {
								advance(); // consume '>'
								break;
							}

							if (peek() == ","_tok) {
								advance(); // consume ','
								continue;
							}
						}
					}
				}  // End of else block for !is_array_subscript
			}

				// Expression is not a numeric literal or evaluable constant - fall through to type parsing
			}
		}

		// Expression parsing failed or wasn't a numeric literal - try parsing a type
try_type_template_argument_parse:
		restore_token_position(arg_saved_pos);
		auto sourceSpellsTemplateId = [&]() {
			SaveHandle lookahead_pos = save_token_position();
			ScopeGuard lookahead_guard([&]() {
				discard_saved_token(lookahead_pos);
			});
			if (peek() == "::"_tok) {
				advance();
			}
			if (!peek().is_identifier()) {
				restore_token_position(lookahead_pos);
				return false;
			}
			advance();
			while (peek() == "::"_tok) {
				advance();
				if (peek() == "template"_tok) {
					advance();
				}
				if (!peek().is_identifier()) {
					restore_token_position(lookahead_pos);
					return false;
				}
				advance();
			}
			const bool has_template_arguments = peek() == "<"_tok;
			restore_token_position(lookahead_pos);
			return has_template_arguments;
		};
		const bool source_spells_template_id = sourceSpellsTemplateId();
		auto sourceTemplateIdNamesVariableTemplate = [&]() {
			SaveHandle lookahead_pos = save_token_position();
			ScopeGuard lookahead_guard([&]() {
				discard_saved_token(lookahead_pos);
			});
			if (peek() == "::"_tok) {
				advance();
			}
			if (!peek().is_identifier()) {
				restore_token_position(lookahead_pos);
				return false;
			}
			StringHandle first_name = peek_info().handle();
			StringHandle candidate_name = first_name;
			advance();
			StringBuilder qualified_name_builder;
			qualified_name_builder.append(StringTable::getStringView(first_name));
			while (peek() == "::"_tok) {
				advance();
				if (peek() == "template"_tok) {
					advance();
				}
				if (!peek().is_identifier()) {
					restore_token_position(lookahead_pos);
					return false;
				}
				candidate_name = peek_info().handle();
				qualified_name_builder.append("::").append(peek_info().value());
				advance();
			}
			const bool has_template_arguments = peek() == "<"_tok;
			std::string_view qualified_name = qualified_name_builder.commit();
			bool names_variable_template =
				gTemplateRegistry.lookupVariableTemplate(qualified_name).has_value() ||
				gTemplateRegistry.lookupVariableTemplate(candidate_name).has_value();
			if (!names_variable_template && qualified_name.find("::") == std::string_view::npos) {
				NamespaceHandle current_namespace = gSymbolTable.get_current_namespace_handle();
				if (!current_namespace.isGlobal()) {
					StringHandle qualified_handle =
						gNamespaceRegistry.buildQualifiedIdentifier(current_namespace, candidate_name);
					names_variable_template =
						qualified_handle.isValid() &&
						gTemplateRegistry.lookupVariableTemplate(qualified_handle).has_value();
				}
			}
			restore_token_position(lookahead_pos);
			return has_template_arguments && names_variable_template;
		};
		const bool source_spells_variable_template_id = sourceTemplateIdNamesVariableTemplate();
		if (source_spells_variable_template_id && !currentArgRejectsValueLikeParsing()) {
			auto value_expr_result = parse_expression(2, ExpressionContext::TemplateTypeArg);
			if (!value_expr_result.is_error() && value_expr_result.node().has_value()) {
				if (peek() == ">>"_tok) {
					split_right_shift_token();
				}
				if (!peek().is_eof() &&
					(peek() == ","_tok || peek() == ">"_tok || peek() == "..."_tok)) {
					const ExpressionNode& value_expr =
						value_expr_result.node()->as<ExpressionNode>();
					StringHandle dependent_name;
					if (const auto* id = std::get_if<IdentifierNode>(&value_expr)) {
						dependent_name = StringTable::getOrInternStringHandle(id->name());
					} else if (const auto* qual_id =
								   std::get_if<QualifiedIdentifierNode>(&value_expr)) {
						dependent_name = StringTable::getOrInternStringHandle(qual_id->full_name());
					}
					TemplateTypeArg dependent_arg = TemplateTypeArg::makeDependentValue(
						dependent_name,
						dependentExpressionValueCategory(
							value_expr,
							dependent_name.isValid()
								? std::optional<StringHandle>(dependent_name)
								: std::nullopt),
						0,
						*value_expr_result.node());
					if (peek() == "..."_tok) {
						advance();
						dependent_arg.is_pack = true;
					}
					template_args.push_back(dependent_arg);
					if (out_type_nodes) {
						out_type_nodes->push_back(*value_expr_result.node());
					}
					if (peek() == ">"_tok) {
						discard_saved_token(arg_saved_pos);
						advance();
						break;
					}
					if (peek() == ","_tok) {
						discard_saved_token(arg_saved_pos);
						advance();
						continue;
					}
				}
			}
			restore_token_position(arg_saved_pos);
		}
		auto type_result = parse_type_specifier();
		if (type_result.is_error() || !type_result.node().has_value()) {
			// Neither type nor expression parsing worked
			FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments failed to parse type or expression (might be comparison operator)");
			restore_token_position(saved_pos);
			last_failed_template_arg_parse_handle_ = saved_pos;
			return std::nullopt;
		}

		// Successfully parsed a type
		TypeSpecifierNode& type_node = type_result.node()->as<TypeSpecifierNode>();

		MemberPointerKind member_pointer_kind = MemberPointerKind::None;

		// Detect pointer-to-member declarator: ClassType::*
		if (peek().is_identifier()) {
			SaveHandle member_saved_pos = save_token_position();
			advance(); // consume class/struct identifier
			if (peek() == "::"_tok) {
				advance(); // consume '::'
				if (peek() == "*"_tok) {
					advance(); // consume '*'
					member_pointer_kind = MemberPointerKind::Object;
					type_node.add_pointer_level(CVQualifier::None);
				} else {
					restore_token_position(member_saved_pos);
				}
			} else {
				restore_token_position(member_saved_pos);
			}
		}

		// Check for postfix cv-qualifiers: T const, T volatile, T const volatile
		// This is the C++ postfix const/volatile syntax used in standard library headers
		// (e.g., "template<typename T> struct is_const<T const>" from <type_traits>)
		while (!peek().is_eof()) {
			if (peek() == "const"_tok) {
				advance();
				type_node.add_cv_qualifier(CVQualifier::Const);
			} else if (peek() == "volatile"_tok) {
				advance();
				type_node.add_cv_qualifier(CVQualifier::Volatile);
			} else {
				break;
			}
		}

		CallingConvention direct_function_calling_convention = CallingConvention::Default;
		if (peek().is_identifier()) {
			SaveHandle calling_convention_pos = save_token_position();
			direct_function_calling_convention = parse_calling_convention(direct_function_calling_convention);
			if (direct_function_calling_convention == CallingConvention::Default || peek() != "("_tok) {
				restore_token_position(calling_convention_pos);
				direct_function_calling_convention = CallingConvention::Default;
			} else {
				discard_saved_token(calling_convention_pos);
			}
		}

		// Check for pointer-to-array syntax: T(*)[] or T(*)[N]
		// AND function pointer/reference syntax: T(&)() or T(*)() or T(&&)()
		// This is the syntax used for pointer-to-array types and function types in template arguments
		// e.g., is_convertible<_FromElementType(*)[], _ToElementType(*)[]>
		// e.g., declval<_Xp(&)()>() - function reference type
		if (peek() == "("_tok) {
			SaveHandle paren_saved_pos = save_token_position();
			advance(); // consume '('

			// Skip optional calling convention before ptr-operator, consistent with
			// parse_declarator() and parse_type_and_name() which call parse_calling_convention()
			// at the same position. Handles patterns like: _Ret (__cdecl _Arg0::*)(_Types...)
			CallingConvention paren_calling_convention = parse_calling_convention(CallingConvention::Default);

			// Detect what's inside: *, &, &&, or _Class::* (member pointer)
			bool is_ptr = false;
			bool is_lvalue_ref = false;
			bool is_rvalue_ref = false;
			bool is_member_ptr = false;
			Token member_ptr_class_token;

			if (!peek().is_eof()) {
				if (peek() == "*"_tok) {
					is_ptr = true;
					advance(); // consume '*'
				} else if (peek() == "&&"_tok) {
					is_rvalue_ref = true;
					advance(); // consume '&&'
				} else if (peek() == "&"_tok) {
					is_lvalue_ref = true;
					advance(); // consume '&'
				} else if (peek().is_identifier()) {
					// Check for member pointer syntax: _Class::*
					SaveHandle member_check_pos = save_token_position();
					member_ptr_class_token = peek_info();
					advance(); // consume class name
					if (peek() == "::"_tok) {
						advance(); // consume '::'
						if (peek() == "*"_tok) {
							advance(); // consume '*'
							is_member_ptr = true;
							is_ptr = true;
							discard_saved_token(member_check_pos);
						} else {
							restore_token_position(member_check_pos);
						}
					} else {
						restore_token_position(member_check_pos);
					}
				}
			}

			if ((is_ptr || is_lvalue_ref || is_rvalue_ref) &&
				peek() == ")"_tok) {
				advance(); // consume ')'

				// Check what follows: [] for array or () for function
				if (peek() == "["_tok) {
					// Pointer-to-array: T(*)[] or T(*)[N]
					if (is_ptr) {
						advance(); // consume '['

						// Optional array size
						std::optional<size_t> ptr_array_size;
						if (peek() != "]"_tok) {
							auto size_result = parse_expression(0, ExpressionContext::TemplateTypeArg);
							if (!size_result.is_error() && size_result.node().has_value()) {
								if (auto const_size = try_evaluate_constant_expression(*size_result.node())) {
									if (const_size->value >= 0) {
										ptr_array_size = static_cast<size_t>(const_size->value);
									}
								}
							}
						}

						if (consume("]"_tok)) {
							// Successfully parsed T(*)[] or T(*)[N]
							// This is a pointer to array - add pointer level and mark as array
							type_node.add_pointer_level(CVQualifier::None);
							type_node.set_array(true, ptr_array_size);
							discard_saved_token(paren_saved_pos);
							FLASH_LOG(Parser, Debug, "Parsed pointer-to-array type T(*)[]");
						} else {
							restore_token_position(paren_saved_pos);
						}
					} else {
						// References to arrays are less common, restore for now
						restore_token_position(paren_saved_pos);
					}
				} else if (peek() == "("_tok) {
					// Function pointer/reference/member: T(&)(...) or T(*)(...) or T(&&)(...) or T(Class::*)(...)
					advance(); // consume '('

					// Parse parameter list using shared helper
					std::vector<TypeIndex> param_types;
					bool param_parse_ok = parse_function_type_parameter_list(param_types);

					if (!param_parse_ok) {
						// Parsing failed - restore position
						restore_token_position(paren_saved_pos);
					}

					if (param_parse_ok && peek() == ")"_tok) {
						advance(); // consume ')'

						// Parse trailing cv-qualifiers, ref-qualifiers, and noexcept
						// For member function pointers: _Res (_Class::*)(_ArgTypes...) const & noexcept
						// For function pointers: _Res(*)(_ArgTypes...) noexcept(_NE)
						// For function references: _Res(&)(_ArgTypes...) noexcept
						bool sig_is_const = false;
						bool sig_is_volatile = false;
						ReferenceQualifier sig_function_ref_qualifier = ReferenceQualifier::None;
						bool sig_is_noexcept = false;
						while (!peek().is_eof()) {
							if ((is_member_ptr) && peek() == "const"_tok) {
								sig_is_const = true;
								advance();
							} else if ((is_member_ptr) && peek() == "volatile"_tok) {
								sig_is_volatile = true;
								advance();
							} else if (is_member_ptr && peek() == "&"_tok) {
								sig_function_ref_qualifier = ReferenceQualifier::LValueReference;
								advance();
							} else if (is_member_ptr && peek() == "&&"_tok) {
								sig_function_ref_qualifier = ReferenceQualifier::RValueReference;
								advance();
							} else if (peek() == "noexcept"_tok) {
								advance(); // consume 'noexcept'
								sig_is_noexcept = parse_noexcept_value();
							} else {
								break;
							}
						}

						// Successfully parsed function reference/pointer type!
						// Capture the return type BEFORE rewriting type_node (mirrors
						// Parser_Decl_DeclaratorCore.cpp which creates a fresh FunctionPointer node).
						FunctionSignature func_sig;
						func_sig.return_type_index = type_node.type_index();
						func_sig.return_pointer_depth = static_cast<int>(type_node.pointer_depth());
						func_sig.return_reference_qualifier = type_node.reference_qualifier();
						func_sig.parameter_type_indices = std::move(param_types);
						func_sig.calling_convention = paren_calling_convention;
						func_sig.is_const = sig_is_const;
						func_sig.is_volatile = sig_is_volatile;
						func_sig.function_reference_qualifier = sig_function_ref_qualifier;
						func_sig.is_noexcept = sig_is_noexcept;

						if (is_member_ptr) {
							type_node.set_type_index(nativeTypeIndex(TypeCategory::MemberFunctionPointer));
							type_node.set_size_in_bits(64);
							type_node.limit_pointer_depth(0);
						} else if (is_ptr) {
							// Rewrite type_node to canonical FunctionPointer form.
							// A function pointer type is TypeCategory::FunctionPointer, 64-bit, with no
							// extra pointer level — the "pointer-ness" is intrinsic to FunctionPointer.
							// (Compare with Parser_Decl_DeclaratorCore.cpp which does the same.)
							type_node.set_type_index(nativeTypeIndex(TypeCategory::FunctionPointer));
							type_node.set_size_in_bits(64);
							type_node.limit_pointer_depth(0);
						}
						type_node.set_function_signature(func_sig);

						if (is_member_ptr) {
							type_node.set_member_class_name(
								member_ptr_class_token.handle());
						}

						if (is_lvalue_ref) {
							type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);	// lvalue reference
						} else if (is_rvalue_ref) {
							type_node.set_reference_qualifier(ReferenceQualifier::RValueReference);	// rvalue reference
						}

						discard_saved_token(paren_saved_pos);
						FLASH_LOG(Parser, Debug, "Parsed function ",
								  is_member_ptr ? "member pointer" : (is_ptr ? "pointer" : (is_rvalue_ref ? "rvalue ref" : "lvalue ref")),
								  " type in template argument");
					} else {
						// Parsing failed - restore position
						restore_token_position(paren_saved_pos);
					}
				} else {
					// Just (*) or (&) or (&&) without [] or () - restore
					restore_token_position(paren_saved_pos);
				}
			} else {
				// Not (*, &, &&, or Class::*) - could be a bare function type: _Res(_ArgTypes...)
				// Try to parse the contents as a parameter list
				// Save position within the parens
				SaveHandle func_type_saved_pos = save_token_position();
				bool is_bare_func_type = false;
				std::vector<TypeIndex> func_param_types;

				// Try to parse as function parameter list using shared helper
				bool param_parse_ok = parse_function_type_parameter_list(func_param_types);

				if (param_parse_ok && peek() == ")"_tok) {
					advance(); // consume ')'
					is_bare_func_type = true;

					// Successfully parsed bare function type
					FunctionSignature func_sig;
					func_sig.return_type_index = type_node.type_index();
					func_sig.return_pointer_depth = static_cast<int>(type_node.pointer_depth());
					func_sig.return_reference_qualifier = type_node.reference_qualifier();
					func_sig.parameter_type_indices = std::move(func_param_types);
					func_sig.calling_convention = direct_function_calling_convention;
					while (!peek().is_eof()) {
						if (peek() == "const"_tok) {
							func_sig.is_const = true;
							advance();
						} else if (peek() == "volatile"_tok) {
							func_sig.is_volatile = true;
							advance();
						} else if (peek() == "&"_tok) {
							func_sig.function_reference_qualifier = ReferenceQualifier::LValueReference;
							advance();
						} else if (peek() == "&&"_tok) {
							func_sig.function_reference_qualifier = ReferenceQualifier::RValueReference;
							advance();
						} else {
							break;
						}
					}
					if (peek() == "noexcept"_tok) {
						advance(); // consume 'noexcept'
						func_sig.is_noexcept = parse_noexcept_value();
					}
					type_node.set_function_signature(func_sig);

					discard_saved_token(func_type_saved_pos);
					discard_saved_token(paren_saved_pos);
					FLASH_LOG(Parser, Debug, "Parsed bare function type in template argument");
				}

				if (!is_bare_func_type) {
					restore_token_position(func_type_saved_pos);
					restore_token_position(paren_saved_pos);
				}
			}
		}

		// Apply pointer/reference modifiers to the type
		consume_pointer_ref_modifiers(type_node);

		if (template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe &&
			(type_node.category() == TypeCategory::UserDefined || type_node.category() == TypeCategory::TypeAlias)) {
			StringHandle type_name_handle = type_node.token().handle();
			auto subst_it = sfinae_type_map_.find(type_name_handle);
			if (subst_it != sfinae_type_map_.end() && subst_it->second.is_valid()) {
				TypeIndex substituted_index = subst_it->second;
				TypeCategory substituted_category = substituted_index.category();
				type_node.set_type_index(substituted_index.withCategory(substituted_category));
				type_node.set_category(substituted_category);
				const int substituted_size_bits = getTypeSpecSizeBits(type_node);
				if (substituted_size_bits > 0) {
					type_node.set_size_in_bits(substituted_size_bits);
				}
			}
		}

		// Check for array declarators (e.g., T[], T[N])
		bool is_array_type = false;
		std::optional<size_t> parsed_array_size;
		while (peek() == "["_tok) {
			is_array_type = true;
			advance(); // consume '['

			// Optional size expression
			if (peek() != "]"_tok) {
				auto size_result = parse_expression(0, ExpressionContext::TemplateTypeArg);
				if (size_result.is_error() || !size_result.node().has_value()) {
					restore_token_position(saved_pos);
					last_failed_template_arg_parse_handle_ = saved_pos;
					return std::nullopt;
				}

				if (auto const_size = try_evaluate_constant_expression(*size_result.node())) {
					if (const_size->value >= 0) {
						parsed_array_size = static_cast<size_t>(const_size->value);
					}
				} else {
					// Size expression present but not evaluable (e.g., template parameter N)
					// Use SIZE_MAX as a sentinel to indicate "sized array with unknown size"
					parsed_array_size = SIZE_MAX;
				}
			}

			if (!consume("]"_tok)) {
				restore_token_position(saved_pos);
				last_failed_template_arg_parse_handle_ = saved_pos;
				return std::nullopt;
			}
		}

		if (is_array_type) {
			type_node.set_array(true, parsed_array_size);
		}

		// Check for pack expansion (...)
		bool is_pack_expansion = false;
		if (peek() == "..."_tok) {
			advance(); // consume '...'
			is_pack_expansion = true;
		}

		// Create TemplateTypeArg from the fully parsed type
		TemplateTypeArg arg(type_node);
		arg.is_pack = is_pack_expansion;
		arg.member_pointer_kind = member_pointer_kind;
		if (dependent_member_probe.isValid()) {
			arg.dependent_name = dependent_member_probe;
		}
		if (!arg.is_template_template_arg && !arg.is_value) {
			StringHandle template_name_handle = type_node.token().handle();
			const TypeInfo* parsed_type_info = type_node.type_index().is_valid()
				? tryGetTypeInfo(type_node.type_index())
				: nullptr;
			const bool parsed_type_is_template_id =
				parsed_type_info != nullptr && parsed_type_info->isTemplateInstantiation();
			const bool token_names_entire_type =
				parsed_type_info == nullptr ||
				parsed_type_info->name() == template_name_handle;
			if (template_name_handle.isValid() &&
				token_names_entire_type &&
				!parsed_type_is_template_id &&
				!source_spells_template_id &&
				(gTemplateRegistry.lookup_alias_template(template_name_handle).has_value() ||
				 gTemplateRegistry.lookupTemplate(template_name_handle).has_value() ||
				 gTemplateRegistry.isClassTemplate(template_name_handle))) {
				arg = TemplateTypeArg::makeTemplate(template_name_handle);
				arg.is_pack = is_pack_expansion;
				FLASH_LOG(Templates, Debug, "Classified explicit template argument '",
						  StringTable::getStringView(template_name_handle),
						  "' as a template-template argument");
			}
		}
		if (type_node.category() == TypeCategory::UserDefined || type_node.category() == TypeCategory::TypeAlias) {
			StringHandle type_name_handle = StringTable::getOrInternStringHandle(type_node.token().value());
			for (const auto& subst : template_param_substitutions_) {
				if (subst.is_value_param && subst.param_name == type_name_handle) {
					FLASH_LOG(Templates, Debug, "Resolved non-type template argument from substitution: ",
							  type_node.token().value(), " -> ", subst.value);
					arg = TemplateTypeArg::makeValue(subst.value, subst.value_type);
					arg.is_pack = is_pack_expansion;
					break;
				} else if (subst.is_type_param && subst.param_name == type_name_handle) {
					const TemplateTypeArg& substituted_arg = subst.substituted_type;
					TypeIndex substituted_index = substituted_arg.type_index;
					if (!substituted_index.is_valid()) {
						substituted_index = nativeTypeIndex(substituted_arg.typeEnum());
					}
					if (substituted_index.is_valid()) {
						TypeCategory substituted_category = substituted_arg.typeEnum();
						type_node.set_type_index(substituted_index.withCategory(substituted_category));
						type_node.set_category(substituted_category);
						for (const CVQualifier cv : substituted_arg.pointer_cv_qualifiers) {
							type_node.add_pointer_level(cv);
						}
						if (substituted_arg.ref_qualifier != ReferenceQualifier::None) {
							if (type_node.reference_qualifier() == ReferenceQualifier::None) {
								type_node.set_reference_qualifier(substituted_arg.ref_qualifier);
							} else if (type_node.reference_qualifier() == ReferenceQualifier::LValueReference ||
									   substituted_arg.ref_qualifier == ReferenceQualifier::LValueReference) {
								type_node.set_reference_qualifier(ReferenceQualifier::LValueReference);
							} else {
								type_node.set_reference_qualifier(ReferenceQualifier::RValueReference);
							}
						}
						const int substituted_size_bits = getTypeSpecSizeBits(type_node);
						if (substituted_size_bits > 0) {
							type_node.set_size_in_bits(substituted_size_bits);
						}
						arg = TemplateTypeArg(type_node);
						arg.is_pack = is_pack_expansion;
						break;
					}
				}
			}
		}

		// Check if this type is dependent (contains template parameters)
		// A type is dependent if:
		// 1. Its type name is in current_template_param_names_ (it IS a template parameter), AND
		//    we're NOT in SFINAE context (during SFINAE, template params are substituted)
		// 2. Its is_incomplete_instantiation_ flag is set (composite type with unresolved template parameters)
		// 3. It's a UserDefined type with type_index=0 (placeholder)
		FLASH_LOG_FORMAT(
			Templates,
			Debug,
			"Checking dependency for template argument: type={}, type_index={}, sfinae_probe={}",
			static_cast<int>(type_node.type()),
			type_node.type_index(),
			template_instantiation_mode_ == TemplateInstantiationMode::SoftProbe);
		if (!arg.is_value &&
			(type_node.category() == TypeCategory::UserDefined || type_node.category() == TypeCategory::TypeAlias || type_node.category() == TypeCategory::Template)) {
			// Prefer the source token spelling when it exists, but fall back to the
			// canonical gTypeInfo name for qualified/composite cases that lost the token text.
			std::string_view type_name = type_node.token().value();
			FLASH_LOG_FORMAT(Templates, Debug, "UserDefined type, type_name from token: {}", type_name);

			// Also get the full type name from gTypeInfo for composite/qualified types
			// The token may only have the base name (e.g., "remove_reference")
			// but gTypeInfo has the full name (e.g., "remove_reference__Tp::type")
			std::string_view full_type_name;
			TypeIndex idx = type_node.type_index();
			if (const TypeInfo* type_info = tryGetTypeInfo(idx)) {
				full_type_name = StringTable::getStringView(type_info->name());
				FLASH_LOG_FORMAT(Templates, Debug, "Full type name from gTypeInfo: {}", full_type_name);
			}

			if (type_name.empty() && !full_type_name.empty()) {
				type_name = full_type_name;
				FLASH_LOG(Templates, Debug, "Using canonical full type name for dependency check");
			}

			if (!type_name.empty()) {
				auto matches_identifier = [](std::string_view haystack, std::string_view needle) {
					size_t pos = haystack.find(needle);
					auto is_ident_char = [](char ch) {
						return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
					};
					while (pos != std::string_view::npos) {
						bool start_ok = (pos == 0) || !is_ident_char(haystack[pos - 1]);
						bool end_ok = (pos + needle.size() >= haystack.size()) || !is_ident_char(haystack[pos + needle.size()]);
						if (start_ok && end_ok) {
							return true;
						}
						pos = haystack.find(needle, pos + 1);
					}
					return false;
				};

				// Check if this is a template parameter name
				// During SFINAE context (re-parsing), template parameters are substituted with concrete types
				// so we should NOT mark them as dependent
				bool is_template_param = false;
				if (template_instantiation_mode_ != TemplateInstantiationMode::SoftProbe) {
					for (const auto& param_name : currentTemplateParamNames()) {
						std::string_view param_sv = StringTable::getStringView(param_name);
						if (type_name == param_sv || matches_identifier(type_name, param_sv)) {
							is_template_param = true;
							break;
						}
					}
				}

				const TypeInfo* arg_type_info = tryGetTypeInfo(idx);
				if (is_template_param ||
					(arg_type_info != nullptr &&
						(arg_type_info->isDependentPlaceholder() ||
						 arg_type_info->is_incomplete_instantiation_))) {
					arg.is_dependent = true;
					arg.dependent_name = StringTable::getOrInternStringHandle(type_name);
					FLASH_LOG_FORMAT(Templates, Debug, "Template argument is dependent (type name: {})", type_name);
				} else if (template_instantiation_mode_ != TemplateInstantiationMode::SoftProbe) {
					// Also check the full type name from gTypeInfo for composite/qualified types
					std::string_view check_name = !full_type_name.empty() ? full_type_name : type_name;

					// Check if this is a qualified identifier (contains ::) which might be a member access
					// If so, check if the base part contains any template parameter
					size_t scope_pos = check_name.find("::");
					if (scope_pos != std::string_view::npos) {
						// This is a qualified identifier - extract the base part (before ::)
						std::string_view base_part = check_name.substr(0, scope_pos);

						for (const auto& param_name : currentTemplateParamNames()) {
							std::string_view param_sv = StringTable::getStringView(param_name);
							// Check both as standalone identifier AND as substring
							// BUT only check substring if the base_part contains underscores (mangled names)
							// This prevents false positives where common substrings match accidentally
							bool contains_param = matches_identifier(base_part, param_sv);
							if (!contains_param && base_part.find('_') != std::string_view::npos) {
								// For mangled names like "remove_reference__Tp", check substring
								contains_param = base_part.find(param_sv) != std::string_view::npos;
							}
							if (contains_param) {
								arg.is_dependent = true;
								arg.dependent_name = StringTable::getOrInternStringHandle(check_name);
								FLASH_LOG_FORMAT(Templates, Debug, "Template argument marked dependent due to qualified identifier with template param: {}", check_name);
								break;
							}
						}
					}
				}
			}

			// Dependent placeholders must be registered with a canonical TypeIndex by
			// their template-parameter owner before they reach argument classification.
			if (!arg.is_dependent && !type_node.type_index().is_valid()) {
				const StringHandle token_name = type_node.token().handle();
				bool handled_by_target_param = false;
				if (const TemplateParameterNode* target_param = currentTargetTemplateParam();
					target_param != nullptr &&
					target_param->nameHandle() == token_name) {
					handled_by_target_param = true;
					if (target_param->kind() == TemplateParameterKind::NonType) {
						TypeCategory non_type_category = target_param->has_type()
							? target_param->type_specifier_node().type()
							: TypeCategory::Int;
						bool was_pack = arg.is_pack;
						arg = TemplateTypeArg::makeDependentValue(token_name, non_type_category);
						arg.is_pack = was_pack;
						FLASH_LOG(Templates, Debug, "Registered target non-type template parameter as dependent template argument");
					} else {
						TypeIndex recovered_type_index = target_param->registered_type_index();
						if (!recovered_type_index.is_valid()) {
							if (const TypeInfo* target_type_info = findTypeByName(token_name)) {
								recovered_type_index =
									target_type_info->type_index_.withCategory(TypeCategory::UserDefined);
							} else {
								TypeInfo& type_info = add_template_param_type(
									token_name,
									target_param->kind() == TemplateParameterKind::Template
										? TypeCategory::Template
										: TypeCategory::UserDefined,
									0);
								type_info.placeholder_kind_ = DependentPlaceholderKind::DependentArgs;
								recovered_type_index = type_info.type_index_.withCategory(
									target_param->kind() == TemplateParameterKind::Template
										? TypeCategory::Template
										: TypeCategory::UserDefined);
							}
						}
						type_node.set_type_index(recovered_type_index);
						arg.type_index = recovered_type_index;
						arg.is_dependent = true;
						arg.dependent_name = token_name;
						FLASH_LOG(Templates, Debug, "Registered target type template parameter as dependent template argument");
					}
				}
				if (!handled_by_target_param) {
					const auto& current_param_names = currentTemplateParamNames();
					bool is_current_template_param = std::any_of(
						current_param_names.begin(),
						current_param_names.end(),
						[token_name](StringHandle param_name) {
							return param_name == token_name;
						});
					if (is_current_template_param) {
						auto param_kind = currentTemplateParamKind(token_name);
						if (param_kind.has_value() && *param_kind == TemplateParameterKind::NonType) {
							TypeCategory non_type_category =
								currentTemplateParamNonTypeCategory(token_name).value_or(TypeCategory::Int);
							bool was_pack = arg.is_pack;
							arg = TemplateTypeArg::makeDependentValue(token_name, non_type_category);
							arg.is_pack = was_pack;
							FLASH_LOG(Templates, Debug, "Registered missing dependent non-type template argument");
						} else {
							TypeInfo& type_info = add_template_param_type(token_name, TypeCategory::UserDefined, 0);
							type_info.placeholder_kind_ = DependentPlaceholderKind::DependentArgs;
							type_node.set_type_index(type_info.type_index_.withCategory(TypeCategory::UserDefined));
							arg.type_index = type_node.type_index();
							arg.is_dependent = true;
							arg.dependent_name = token_name;
							FLASH_LOG(Templates, Debug, "Registered missing dependent placeholder TypeIndex for template argument");
						}
					}
					else if (findTypeByName(token_name) == nullptr) {
						auto [simple_identifier_kind, prebuilt_arg] = classifySimpleTemplateArgName(token_name);
						if (simple_identifier_kind == SimpleTemplateArgKind::ValueLike) {
							bool was_pack = arg.is_pack;
							if (prebuilt_arg.has_value() && prebuilt_arg->is_value) {
								arg = *prebuilt_arg;
							} else {
								TypeCategory non_type_category =
									dependentIdentifierValueCategory(token_name).value_or(TypeCategory::Int);
								arg = TemplateTypeArg::makeDependentValue(token_name, non_type_category);
							}
							arg.is_pack = was_pack;
							FLASH_LOG(Templates, Debug, "Recovered unresolved template argument as value-like identifier");
						} else if (parsing_template_depth_ > 0 ||
								   !struct_parsing_context_stack_.empty()) {
							arg.type_index = nativeTypeIndex(TypeCategory::UserDefined);
							arg.is_value = false;
							arg.is_dependent = true;
							arg.dependent_name = token_name;
							FLASH_LOG(Templates, Debug, "Recovered unresolved template argument as dependent type identifier");
						} else {
							restore_token_position(saved_pos);
							last_failed_template_arg_parse_handle_ = saved_pos;
							return std::nullopt;
						}
					}
					else {
						const TypeInfo* known_type_info = findTypeByName(token_name);
						if (known_type_info != nullptr &&
							(known_type_info->isDependentPlaceholder() ||
							 known_type_info->is_incomplete_instantiation_)) {
							TypeIndex recovered_type_index =
								known_type_info->type_index_.withCategory(TypeCategory::UserDefined);
							type_node.set_type_index(recovered_type_index);
							arg.type_index = recovered_type_index;
							arg.is_dependent = true;
							arg.dependent_name = token_name;
							FLASH_LOG(Templates, Debug, "Reattached known dependent placeholder TypeIndex for template argument");
						} else if (parsing_template_depth_ > 0 ||
								   !struct_parsing_context_stack_.empty()) {
							arg.type_index = nativeTypeIndex(TypeCategory::UserDefined);
							arg.is_value = false;
							arg.is_dependent = true;
							arg.dependent_name = token_name;
							FLASH_LOG(Templates, Debug, "Recovered known-name template argument as dependent type identifier");
						} else {
							throw InternalError("Unregistered dependent placeholder type reached template argument classification");
						}
					}
				}
			}
		}

		// Also check Struct types - if this is a template class that was parsed with dependent arguments,
		// the instantiation was skipped and we got back the primary template type
		// In a template body, if the struct is a registered template and we're using template params, it's dependent
		// BUT: If this is a template template argument (passing a template class as an argument), it's NOT dependent
		// even if we're in a template body. A template class like HasType used as a template argument is concrete.
		if (!arg.is_dependent &&
			type_node.category() == TypeCategory::Struct &&
			parsing_template_depth_ > 0 &&
			template_instantiation_mode_ != TemplateInstantiationMode::SoftProbe) {
			TypeIndex idx = type_node.type_index();
			if (const TypeInfo* type_info = tryGetTypeInfo(idx)) {
				std::string_view type_name = StringTable::getStringView(type_info->name());
				// Check if this is a template primary (not an instantiation which would have underscores)
				TemplateNameLookupRequest primary_lookup_request =
					buildTemplateNameLookupRequest(
						StringTable::getOrInternStringHandle(type_name),
						TemplateNameLookupKind::Ordinary,
						false);
				TemplateNameLookupResult primary_lookup =
					gTemplateRegistry.lookupTemplateName(primary_lookup_request);
				auto template_opt = primary_lookup.firstDeclarationOfKind(
					TemplateDeclarationKind::ClassTemplate);
				if (template_opt.has_value() && template_opt->is<TemplateClassDeclarationNode>()) {
					// This struct type is a template primary
					// Check if type_name contains any current template parameters
					// If not, it's a concrete template class being used as a template template argument
					bool contains_template_param = false;
					for (const auto& param_name : currentTemplateParamNames()) {
						if (type_name == param_name) {
							contains_template_param = true;
							break;
						}
					}

					// Only mark as dependent if the type name itself is a template parameter
					// A template class like HasType being used as an argument is NOT dependent
					if (contains_template_param) {
						FLASH_LOG_FORMAT(Templates, Debug, "Template argument {} is primary template matching template param - marking as dependent", type_name);
						arg.is_dependent = true;
						arg.dependent_name = StringTable::getOrInternStringHandle(type_name);
					} else {
						FLASH_LOG_FORMAT(Templates, Debug, "Template argument {} is a concrete template class (used as template template arg) - NOT dependent", type_name);
					}
				}
			}
		}

		template_args.push_back(arg);
		if (out_type_nodes) {
			out_type_nodes->push_back(*type_result.node());
		}

		// Check for ',' or '>'
		if (peek().is_eof()) {
			FLASH_LOG(Parser, Error, "parse_explicit_template_arguments unexpected end of tokens");
			restore_token_position(saved_pos);
			last_failed_template_arg_parse_handle_ = saved_pos;
			return std::nullopt;
		}

		FLASH_LOG_FORMAT(Parser, Debug, "After adding type argument, peek_token={}", std::string(peek_info().value()));

		// Phase 5: Handle >> token splitting for nested templates
		// C++20 maximal munch: Foo<Bar<int>> should parse as Foo<Bar<int> >
		if (peek() == ">>"_tok) {
			FLASH_LOG(Parser, Debug, "Encountered >> token, splitting for nested template");
			split_right_shift_token();
		}

		if (peek() == ">"_tok) {
			advance(); // consume '>'
			break;
		}

		if (peek() == ","_tok) {
			advance(); // consume ','
			continue;
		}

		// Unexpected token
		FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments unexpected token: '", peek_info().value(), "' (might be comparison operator)");
		restore_token_position(saved_pos);
		last_failed_template_arg_parse_handle_ = saved_pos;
		return std::nullopt;
	}

	// Success - discard saved position
	discard_saved_token(saved_pos);
	last_failed_template_arg_parse_handle_ = SIZE_MAX;  // Clear failure marker on success
	return template_args;
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::parse_explicit_template_arguments(
	InlineVector<ASTNode, 4>* out_type_nodes) {
	if (out_type_nodes == nullptr) {
		return parse_explicit_template_arguments();
	}

	std::vector<ASTNode> parsed_type_nodes;
	auto parsed_args = parse_explicit_template_arguments(&parsed_type_nodes);
	if (!parsed_args.has_value()) {
		return std::nullopt;
	}

	*out_type_nodes = std::move(parsed_type_nodes);
	return parsed_args;
}

void Parser::classifyExplicitTemplateArgumentsAgainstParameters(
	std::span<const TemplateParameterNode> target_template_params,
	InlineVector<TemplateTypeArg, 4>& template_args,
	const std::vector<ASTNode>* argument_syntax_nodes) {
	if (target_template_params.empty() || template_args.empty()) {
		return;
	}

	auto syntaxNodeForArg = [&](size_t index) -> const ASTNode* {
		if (argument_syntax_nodes == nullptr || index >= argument_syntax_nodes->size()) {
			return nullptr;
		}
		return &(*argument_syntax_nodes)[index];
	};

	auto nameFromExpression = [](const ExpressionNode& expr) -> StringHandle {
		if (const auto* id = std::get_if<IdentifierNode>(&expr)) {
			return StringTable::getOrInternStringHandle(id->name());
		}
		if (const auto* tparam_ref = std::get_if<TemplateParameterReferenceNode>(&expr)) {
			return tparam_ref->param_name();
		}
		if (const auto* qual_id = std::get_if<QualifiedIdentifierNode>(&expr)) {
			return StringTable::getOrInternStringHandle(qual_id->full_name());
		}
		return StringHandle{};
	};

	auto nameFromSyntaxOrArg = [&](const ASTNode* syntax_node, const TemplateTypeArg& arg) -> StringHandle {
		if (syntax_node != nullptr) {
			if (syntax_node->is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = syntax_node->as<TypeSpecifierNode>();
				if (!type_spec.token().value().empty()) {
					return StringTable::getOrInternStringHandle(type_spec.token().value());
				}
				if (const TypeInfo* type_info = tryGetTypeInfo(type_spec.type_index())) {
					return type_info->name();
				}
			} else if (syntax_node->is<ExpressionNode>()) {
				StringHandle expr_name = nameFromExpression(syntax_node->as<ExpressionNode>());
				if (expr_name.isValid()) {
					return expr_name;
				}
			}
		}
		if (arg.is_template_template_arg && arg.template_name_handle.isValid()) {
			return arg.template_name_handle;
		}
		if (arg.dependent_name.isValid()) {
			return arg.dependent_name;
		}
		if (const TypeInfo* type_info = tryGetTypeInfo(arg.type_index)) {
			return type_info->name();
		}
		return StringHandle{};
	};

	auto makeTypeArgForName = [&](StringHandle name) -> std::optional<TemplateTypeArg> {
		if (!name.isValid()) {
			return std::nullopt;
		}
		if (const TypeInfo* type_info = findTypeByName(name)) {
			TemplateTypeArg result = TemplateTypeArg::makeType(
				type_info->type_index_.withCategory(TypeCategory::UserDefined));
			if (type_info->isDependentPlaceholder() || type_info->is_incomplete_instantiation_) {
				result.is_dependent = true;
				result.dependent_name = name;
			}
			return result;
		}
		if (auto param_kind = currentTemplateParamKind(name);
			param_kind.has_value() && *param_kind != TemplateParameterKind::NonType) {
			TemplateTypeArg result;
			result.type_index = nativeTypeIndex(TypeCategory::UserDefined);
			result.is_dependent = true;
			result.dependent_name = name;
			return result;
		}
		if (gTemplateRegistry.lookup_alias_template(name).has_value() ||
			gTemplateRegistry.isClassTemplate(name)) {
			TemplateTypeArg result;
			result.type_index = nativeTypeIndex(TypeCategory::UserDefined);
			result.is_dependent = true;
			result.dependent_name = name;
			return result;
		}
		return std::nullopt;
	};

	auto hasConcreteSubstitutionForName = [&](StringHandle name) {
		if (!name.isValid()) {
			return false;
		}
		for (const auto& subst : template_param_substitutions_) {
			if (subst.param_name == name) {
				return true;
			}
		}
		return false;
	};

	auto expressionHasUnsubstitutedDependency =
		[&](const auto& self, const ASTNode& node) -> bool {
			if (node.is<TypeSpecifierNode>()) {
				const TypeSpecifierNode& type_spec = node.as<TypeSpecifierNode>();
				return typeSpecStillUsesDependentPlaceholder(type_spec);
			}
			if (!node.is<ExpressionNode>()) {
				return false;
			}
			const ExpressionNode& dep_expr = node.as<ExpressionNode>();
			if (const auto* tparam_ref =
					std::get_if<TemplateParameterReferenceNode>(&dep_expr)) {
				return !hasConcreteSubstitutionForName(tparam_ref->param_name());
			}
			if (const auto* id = std::get_if<IdentifierNode>(&dep_expr)) {
				StringHandle name = StringTable::getOrInternStringHandle(id->name());
				return currentTemplateParamKind(name).has_value() &&
					   !hasConcreteSubstitutionForName(name);
			}
			if (const auto* qual_id = std::get_if<QualifiedIdentifierNode>(&dep_expr)) {
				auto storedArgsContainUnsubstitutedDependency =
					[&](std::span<const TypeInfo::TemplateArgInfo> stored_args) {
					for (const TypeInfo::TemplateArgInfo& stored_arg : stored_args) {
						TemplateTypeArg arg = toTemplateTypeArg(stored_arg);
						if (arg.dependent_name.isValid() &&
							!hasConcreteSubstitutionForName(arg.dependent_name)) {
							return true;
						}
						if (arg.dependent_expr.has_value() &&
							self(self, *arg.dependent_expr)) {
							return true;
						}
						if (arg.is_dependent && !arg.dependent_name.isValid()) {
							return true;
						}
					}
					return false;
				};
				if (const TypeInfo::DependentQualifiedNameRecord* record =
						qual_id->dependentQualifiedName()) {
					if (record->owner_name.isValid() &&
						currentTemplateParamKind(record->owner_name).has_value() &&
						!hasConcreteSubstitutionForName(record->owner_name)) {
						return true;
					}
					if (storedArgsContainUnsubstitutedDependency(
							record->owner_template_arguments)) {
						return true;
					}
					for (const auto& member : record->member_chain) {
						if (storedArgsContainUnsubstitutedDependency(
								member.template_arguments)) {
							return true;
						}
					}
				}
				std::string_view owner_name =
					gNamespaceRegistry.getQualifiedName(qual_id->namespace_handle());
				if (!owner_name.empty()) {
					auto owner_it = getTypesByNameMap().find(
						StringTable::getOrInternStringHandle(owner_name));
					if (owner_it != getTypesByNameMap().end() &&
						owner_it->second != nullptr) {
						const TypeInfo* owner_info = owner_it->second;
						if (owner_info->isDependentPlaceholder() ||
							owner_info->is_incomplete_instantiation_ ||
							owner_info->hasDependentQualifiedName()) {
							if (storedArgsContainUnsubstitutedDependency(
									owner_info->templateArgs())) {
								return true;
							}
						}
					}
				}
				return false;
			}
			if (const auto* binary = std::get_if<BinaryOperatorNode>(&dep_expr)) {
				return self(self, binary->get_lhs()) || self(self, binary->get_rhs());
			}
			if (const auto* unary = std::get_if<UnaryOperatorNode>(&dep_expr)) {
				return self(self, unary->get_operand());
			}
			if (const auto* member = std::get_if<MemberAccessNode>(&dep_expr)) {
				return self(self, member->object());
			}
			if (const auto* ptr_member = std::get_if<PointerToMemberAccessNode>(&dep_expr)) {
				return self(self, ptr_member->object()) || self(self, ptr_member->member_pointer());
			}
			if (const auto* array_subscript = std::get_if<ArraySubscriptNode>(&dep_expr)) {
				return self(self, array_subscript->array_expr()) || self(self, array_subscript->index_expr());
			}
			if (const auto* call_expr = std::get_if<CallExprNode>(&dep_expr)) {
				if (call_expr->has_receiver() && self(self, call_expr->receiver())) {
					return true;
				}
				for (const ASTNode& arg : call_expr->arguments()) {
					if (self(self, arg)) {
						return true;
					}
				}
				for (const ASTNode& template_arg : call_expr->template_arguments()) {
					if (self(self, template_arg)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* ctor_call = std::get_if<ConstructorCallNode>(&dep_expr)) {
				if (typeSpecStillUsesDependentPlaceholder(ctor_call->type_specifier_node())) {
					return true;
				}
				for (const ASTNode& arg : ctor_call->arguments()) {
					if (self(self, arg)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* init_list = std::get_if<InitializerListConstructionNode>(&dep_expr)) {
				if (self(self, init_list->element_type()) || self(self, init_list->target_type())) {
					return true;
				}
				for (const ASTNode& element : init_list->elements()) {
					if (self(self, element)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* noexc = std::get_if<NoexceptExprNode>(&dep_expr)) {
				return self(self, noexc->expr());
			}
			if (const auto* sizeof_expr = std::get_if<SizeofExprNode>(&dep_expr)) {
				return self(self, sizeof_expr->type_or_expr());
			}
			if (const auto* alignof_expr = std::get_if<AlignofExprNode>(&dep_expr)) {
				return self(self, alignof_expr->type_or_expr());
			}
			if (const auto* cast_expr = std::get_if<StaticCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<DynamicCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<ConstCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* cast_expr = std::get_if<ReinterpretCastNode>(&dep_expr)) {
				return self(self, cast_expr->expr()) || typeSpecStillUsesDependentPlaceholder(cast_expr->target_type_node());
			}
			if (const auto* trait_expr = std::get_if<TypeTraitExprNode>(&dep_expr)) {
				if (trait_expr->has_type() && self(self, trait_expr->type_node())) {
					return true;
				}
				if (trait_expr->has_second_type() && self(self, trait_expr->second_type_node())) {
					return true;
				}
				for (const ASTNode& extra_type : trait_expr->additional_type_nodes()) {
					if (self(self, extra_type)) {
						return true;
					}
				}
				return false;
			}
			if (const auto* ternary = std::get_if<TernaryOperatorNode>(&dep_expr)) {
				return self(self, ternary->condition()) ||
					   self(self, ternary->true_expr()) ||
					   self(self, ternary->false_expr());
			}
			return false;
		};

	auto makeValueArgForSyntax = [&](const ASTNode* syntax_node,
									 const TemplateTypeArg& existing_arg,
									 const TemplateParameterNode& param)
		-> std::optional<TemplateTypeArg> {
		if (syntax_node != nullptr && syntax_node->is<ExpressionNode>()) {
			const ASTNode& expr_node = *syntax_node;
			if (!expressionHasUnsubstitutedDependency(
					expressionHasUnsubstitutedDependency,
					expr_node)) {
			if (auto const_value = try_evaluate_constant_expression(expr_node)) {
				FlashCpp::NonTypeValueIdentity identity = const_value->identity;
				if (!identity.member_class_name.isValid() &&
					existing_arg.member_class_name.isValid()) {
					identity.member_class_name = existing_arg.member_class_name;
				}
				if (!identity.function_signature.has_value() &&
					existing_arg.function_signature.has_value()) {
					identity.function_signature = existing_arg.function_signature;
				}
				TypeIndex declared_type_index = param.has_type()
					? param.type_specifier_node().type_index().withCategory(param.type_specifier_node().type())
					: TypeIndex{};
				const TypeCategory declared_category = declared_type_index.category();
				const bool has_declared_concrete_value_type =
					declared_category != TypeCategory::Invalid &&
					declared_category != TypeCategory::Auto &&
					declared_category != TypeCategory::DeclTypeAuto &&
					!typeIndexContainsDependentPlaceholder(declared_type_index);
				if (has_declared_concrete_value_type) {
					identity.value_type_index = declared_type_index.is_valid()
						? declared_type_index
						: nativeTypeIndex(declared_type_index.category());
					if (declared_category == TypeCategory::FunctionPointer) {
						if (identity.kind == FlashCpp::NonTypeValueIdentityKind::ObjectPointer ||
							identity.kind == FlashCpp::NonTypeValueIdentityKind::Nullptr ||
							identity.kind == FlashCpp::NonTypeValueIdentityKind::Reference) {
							identity.kind = FlashCpp::NonTypeValueIdentityKind::FunctionPointer;
						}
					} else if (declared_category == TypeCategory::MemberObjectPointer ||
							   declared_category == TypeCategory::MemberFunctionPointer) {
						if (!identity.member_class_name.isValid() &&
							param.type_specifier_node().has_member_class()) {
							identity.member_class_name = param.type_specifier_node().member_class_name();
						}
						if (!identity.function_signature.has_value() &&
							param.type_specifier_node().has_function_signature()) {
							identity.function_signature = param.type_specifier_node().function_signature();
						}
						if (identity.kind == FlashCpp::NonTypeValueIdentityKind::Nullptr) {
							identity.kind = FlashCpp::NonTypeValueIdentityKind::MemberPointer;
						}
					} else if (param.type_specifier_node().pointer_depth() > 0 &&
							   identity.kind == FlashCpp::NonTypeValueIdentityKind::Nullptr) {
						identity.kind = FlashCpp::NonTypeValueIdentityKind::ObjectPointer;
					}
				}
				if (param.type_specifier_node().is_reference()) {
					StringHandle referenced_name = nameFromExpression(expr_node.as<ExpressionNode>());
					if (referenced_name.isValid()) {
						identity = FlashCpp::NonTypeValueIdentity::makeReference(
							identity.value_type_index,
							referenced_name);
					} else if (identity.kind == FlashCpp::NonTypeValueIdentityKind::ObjectPointer ||
							   identity.kind == FlashCpp::NonTypeValueIdentityKind::FunctionPointer) {
						identity.kind = FlashCpp::NonTypeValueIdentityKind::Reference;
					}
				}
				TemplateTypeArg result = TemplateTypeArg::makeValueIdentity(identity);
				return result;
			}
			}
			if (existing_arg.is_value &&
				existing_arg.valueIdentity().kind != FlashCpp::NonTypeValueIdentityKind::Integral &&
				existing_arg.valueIdentity().kind != FlashCpp::NonTypeValueIdentityKind::Nullptr) {
				return existing_arg;
			}
			const ExpressionNode& expr = syntax_node->as<ExpressionNode>();
			StringHandle name = nameFromExpression(expr);
			TypeCategory category = param.has_type()
				? param.type_specifier_node().type()
				: TypeCategory::Int;
			return TemplateTypeArg::makeDependentValue(name, category, 0, expr_node);
		}

		StringHandle name = nameFromSyntaxOrArg(syntax_node, existing_arg);
		if (name.isValid()) {
			for (const auto& subst : template_param_substitutions_) {
				if (subst.param_name == name && subst.is_value_param) {
					return TemplateTypeArg::makeValue(subst.value, subst.value_type);
				}
			}
			if (param.type_specifier_node().is_reference()) {
				TypeIndex reference_identity_type = nativeTypeIndex(TypeCategory::Int);
				if (param.has_type()) {
					TypeIndex declared_type_index = param.type_specifier_node().type_index().withCategory(param.type_specifier_node().type());
					TypeCategory declared_category = declared_type_index.category();
					if (declared_category != TypeCategory::Invalid &&
						declared_category != TypeCategory::Auto &&
						declared_category != TypeCategory::DeclTypeAuto) {
						reference_identity_type = declared_type_index.is_valid()
							? declared_type_index
							: nativeTypeIndex(declared_category);
					}
				}
				return TemplateTypeArg::makeValueIdentity(
					FlashCpp::NonTypeValueIdentity::makeReference(reference_identity_type, name));
			}
			if (auto symbol_lookup = lookup_symbol_with_template_check(name);
				symbol_lookup.has_value() && symbol_lookup->is<VariableDeclarationNode>()) {
				const VariableDeclarationNode& variable_decl = symbol_lookup->as<VariableDeclarationNode>();
				if (variable_decl.initializer().has_value()) {
					if (auto const_value = try_evaluate_constant_expression(*variable_decl.initializer())) {
						return TemplateTypeArg::makeValueIdentity(const_value->identity);
					}
				}
			}
			TypeCategory category = param.has_type()
				? param.type_specifier_node().type()
				: TypeCategory::Int;
			return TemplateTypeArg::makeDependentValue(name, category);
		}
		return std::nullopt;
	};

	auto makeTemplateTemplateArgForName = [&](StringHandle name) -> std::optional<TemplateTypeArg> {
		if (!name.isValid()) {
			return std::nullopt;
		}
		// Concrete templates (alias, class, or registered): not dependent.
		if (gTemplateRegistry.lookup_alias_template(name).has_value() ||
			gTemplateRegistry.lookupTemplate(name).has_value() ||
			gTemplateRegistry.isClassTemplate(name)) {
			return TemplateTypeArg::makeTemplate(name);
		}
		// Template template parameter: the name is a bound parameter, not a concrete
		// template.  Mark the arg as dependent so that try_instantiate_class_template
		// creates only a dependent placeholder (instead of a premature instantiation).
		// The dependent_name is required so that the arg round-trips correctly through
		// toTemplateArgInfo / toTemplateTypeArg and is recognized as dependent by
		// try_materialize_exact_owner, allowing the correct concrete substitution to
		// happen later during lazy instantiation of the enclosing template.
		if (currentTemplateParamKind(name).has_value() &&
			*currentTemplateParamKind(name) == TemplateParameterKind::Template) {
			TemplateTypeArg arg = TemplateTypeArg::makeTemplate(name);
			arg.is_dependent = true;
			arg.dependent_name = name;
			return arg;
		}
		return std::nullopt;
	};

	size_t arg_index = 0;
	for (size_t param_index = 0;
		 param_index < target_template_params.size() && arg_index < template_args.size();
		 ++param_index) {
		const TemplateParameterNode& param = target_template_params[param_index];
		if (param.is_variadic()) {
			while (arg_index < template_args.size()) {
				const ASTNode* syntax_node = syntaxNodeForArg(arg_index);
				TemplateTypeArg& arg = template_args[arg_index];
				const bool was_pack = arg.is_pack;
				StringHandle arg_name = nameFromSyntaxOrArg(syntax_node, arg);
				std::optional<TemplateTypeArg> reclassified;
				if (param.kind() == TemplateParameterKind::Type && arg.is_value) {
					reclassified = makeTypeArgForName(arg_name);
				} else if (param.kind() == TemplateParameterKind::NonType &&
						   (!arg.is_value ||
							arg.valueIdentity().kind == FlashCpp::NonTypeValueIdentityKind::Integral ||
							arg.valueIdentity().kind == FlashCpp::NonTypeValueIdentityKind::Nullptr)) {
					reclassified = makeValueArgForSyntax(syntax_node, arg, param);
				} else if (param.kind() == TemplateParameterKind::Template &&
						   !arg.is_template_template_arg) {
					reclassified = makeTemplateTemplateArgForName(arg_name);
				}
				if (reclassified.has_value()) {
					reclassified->is_pack = was_pack;
					arg = *reclassified;
				}
				++arg_index;
			}
			break;
		}

		const ASTNode* syntax_node = syntaxNodeForArg(arg_index);
		TemplateTypeArg& arg = template_args[arg_index];
		const bool was_pack = arg.is_pack;
		StringHandle arg_name = nameFromSyntaxOrArg(syntax_node, arg);
		std::optional<TemplateTypeArg> reclassified;
		if (param.kind() == TemplateParameterKind::Type && arg.is_value) {
			reclassified = makeTypeArgForName(arg_name);
		} else if (param.kind() == TemplateParameterKind::NonType &&
				   (!arg.is_value ||
					arg.valueIdentity().kind == FlashCpp::NonTypeValueIdentityKind::Integral ||
					arg.valueIdentity().kind == FlashCpp::NonTypeValueIdentityKind::Nullptr)) {
			reclassified = makeValueArgForSyntax(syntax_node, arg, param);
		} else if (param.kind() == TemplateParameterKind::Template &&
				   !arg.is_template_template_arg) {
			reclassified = makeTemplateTemplateArgForName(arg_name);
		}
		if (reclassified.has_value()) {
			reclassified->is_pack = was_pack;
			arg = *reclassified;
		}
		++arg_index;
	}
}

StringHandle Parser::extractDependentMemberProbeFromCurrentTemplateArg() {
	// O(N) probe: save once, scan forward with advance(), restore at end.
	// The old implementation used peek_info(lookahead) inside a loop which
	// cost O(K) save/restore pairs per iteration — O(N²) total for N tokens.
	StringHandle result{};
	SaveHandle saved_pos = save_token_position();
	int angle_depth = 0;
	int paren_depth = 0;
	int bracket_depth = 0;
	for (size_t steps = 0; steps < 128; ++steps) {
		Token token = peek_info();
		TokenKind kind = token.kind();
		if (kind.is_eof()) {
			break;
		}
		if (kind == "<"_tok) {
			++angle_depth;
		} else if (kind == ">"_tok) {
			if (angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
				break;
			}
			if (angle_depth > 0) {
				--angle_depth;
			}
		} else if (kind == ">>"_tok) {
			if (angle_depth <= 1 && paren_depth == 0 && bracket_depth == 0) {
				break;
			}
			angle_depth = angle_depth > 1 ? angle_depth - 2 : 0;
		} else if (kind == "("_tok) {
			++paren_depth;
		} else if (kind == ")"_tok) {
			if (paren_depth > 0) {
				--paren_depth;
			}
		} else if (kind == "["_tok) {
			++bracket_depth;
		} else if (kind == "]"_tok) {
			if (bracket_depth > 0) {
				--bracket_depth;
			}
		} else if (kind == ","_tok && angle_depth == 0 && paren_depth == 0 && bracket_depth == 0) {
			break;
		}

		if (kind == "typename"_tok) {
			// advance() is below, so current_token_ is still 'typename' here.
			// peek_info(1) = owner identifier, peek_info(2) = first '::'.
			Token owner_token = peek_info(1);
			Token scope_token = peek_info(2);
			if (owner_token.kind().is_eof() ||
				scope_token.kind().is_eof()) {
				break;
			}
			if (owner_token.type() == Token::Type::Identifier &&
				scope_token.kind() == "::"_tok) {
				StringHandle owner_handle = owner_token.handle();
				const auto& current_param_names = currentTemplateParamNames();
				bool owner_is_template_param = std::any_of(
					current_param_names.begin(),
					current_param_names.end(),
					[owner_handle](StringHandle param_name) {
						return param_name == owner_handle;
					});
				if (owner_is_template_param) {
					InlineVector<StringHandle, 4> components;
					components.push_back(owner_handle);
					// chain_lookahead 2 → first '::' after owner (relative to current 'typename')
					size_t chain_lookahead = 2;
					while (peek_info(chain_lookahead).kind() == "::"_tok) {
						Token member_token = peek_info(chain_lookahead + 1);
						if (member_token.kind().is_eof() ||
							member_token.type() != Token::Type::Identifier) {
							break;
						}
						components.push_back(member_token.handle());
						chain_lookahead += 2;
					}
					if (components.size() > 1) {
						result = gNamespaceRegistry.buildQualifiedIdentifier(components);
						break;
					}
				}
			}
		}
		advance();
	}
	// Restore caller's lexer position — this function is a pure read-only probe.
	restore_lexer_position_only(saved_pos);
	discard_saved_token(saved_pos);
	return result;
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::parse_explicit_template_arguments(
	std::span<const TemplateParameterNode> target_template_params,
	std::vector<ASTNode>* out_type_nodes) {
	ScopedExplicitTemplateArgumentTargetParams target_param_guard(*this, target_template_params);
	auto parsed_args = parse_explicit_template_arguments(out_type_nodes);
	if (parsed_args.has_value()) {
		classifyExplicitTemplateArgumentsAgainstParameters(
			target_template_params,
			*parsed_args,
			out_type_nodes);
	}
	return parsed_args;
}

std::optional<InlineVector<TemplateTypeArg, 4>> Parser::parse_explicit_template_arguments(
	std::span<const TemplateParameterNode> target_template_params,
	InlineVector<ASTNode, 4>* out_type_nodes) {
	ScopedExplicitTemplateArgumentTargetParams target_param_guard(*this, target_template_params);
	if (out_type_nodes == nullptr) {
		auto parsed_args = parse_explicit_template_arguments();
		if (parsed_args.has_value()) {
			classifyExplicitTemplateArgumentsAgainstParameters(target_template_params, *parsed_args, nullptr);
		}
		return parsed_args;
	}

	std::vector<ASTNode> parsed_type_nodes;
	auto parsed_args = parse_explicit_template_arguments(target_template_params, &parsed_type_nodes);
	if (!parsed_args.has_value()) {
		return std::nullopt;
	}

	*out_type_nodes = std::move(parsed_type_nodes);
	return parsed_args;
}

// C++20 Template Argument Disambiguation
// Check if '<' at current position could start template arguments without consuming tokens.
// This implements lookahead to disambiguate template argument lists from comparison operators.
Parser::TemplateTypeArgParsingResult Parser::parse_explicit_template_arguments_as_result(TokenDestroyPattern destroy_pattern) {
#if WITH_PARSER_RUNTIME_STATS
	FLASHCPP_PARSER_RUNTIME_PHASE(ExplicitTemplateArgumentProbe);
#endif
	// Quick check: must have '<' at current position
	if (peek() != "<"_tok) {
		return {};
	}

	FLASH_LOG(Parser, Debug, "parse_explicit_template_arguments_as_result: checking if '<' starts template arguments");

	// Save position BEFORE attempting to parse template arguments
	auto saved_pos = save_token_position();

	// Try to parse template arguments speculatively
	auto template_args = parse_explicit_template_arguments();

	if (!template_args.has_value()) {
		FLASH_LOG(Parser, Trace, "parse_explicit_template_arguments_as_result, failed to parse as template argument");

		restore_token_position(saved_pos);
		discard_saved_token(saved_pos);
		return {};
	}

	// A template needs to be followed by a qualifier or a function
	if (peek() != "::"_tok &&
		peek() != "("_tok)
	{
		FLASH_LOG(Parser, Trace, "parse_explicit_template_arguments_as_result, following token is not :: or (, so not treating as a template!");

		restore_token_position(saved_pos);
		discard_saved_token(saved_pos);
		return {};
	}

	return TemplateTypeArgParsingResult{ this, std::move(template_args.value()), saved_pos, destroy_pattern };
}

// Consolidates all qualified identifier parsing into a single, consistent code path.
// This function parses patterns like: A::B::C or ns::Template<Args>::member
std::optional<QualifiedIdParseResult> Parser::parse_qualified_identifier_with_templates() {
	FLASH_LOG(Parser, Debug, "parse_qualified_identifier_with_templates: starting");

	// Must start with an identifier
	if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
		return std::nullopt;
	}

	std::vector<StringHandle> namespaces;
	Token final_identifier = current_token_;
	advance(); // consume first identifier

	// Check if followed by ::
	if (current_token_.kind().is_eof() || current_token_.value() != "::") {
		// Single identifier, no qualification - not a qualified identifier
		// Restore position for caller to handle
		return std::nullopt;
	}

	// Collect namespace parts
	while (current_token_.value() == "::") {
		// Current identifier becomes a namespace part - intern into string table
		namespaces.emplace_back(final_identifier.handle());
		advance(); // consume ::

		// Get next identifier
		if (current_token_.kind().is_eof() || current_token_.type() != Token::Type::Identifier) {
			// Error: expected identifier after ::
			return std::nullopt;
		}
		final_identifier = current_token_;
		advance(); // consume the identifier
	}

	// At this point: current_token_ is the token after final identifier
	// Check for template arguments: A::B::C<Args>
	if (current_token_.value() == "<") {
		FLASH_LOG_FORMAT(Parser, Debug, "parse_qualified_identifier_with_templates: parsing template args for '{}'",
						 final_identifier.value());
		auto template_args = parse_explicit_template_arguments();
		if (template_args.has_value()) {
			FLASH_LOG_FORMAT(Parser, Debug, "parse_qualified_identifier_with_templates: parsed {} template args",
							 template_args->size());
			return QualifiedIdParseResult(namespaces, final_identifier, *template_args);
		}
	}

	// No template arguments or parsing failed
	return QualifiedIdParseResult(namespaces, final_identifier);
}

// Try to instantiate a template with explicit template arguments
