#include "Parser.h"

#include "ConstExprEvaluator.h"
#include "NameMangling.h"
#include "OverloadResolution.h"
#include "ParserTemplateClassShared.h"
#include "TypeTraitEvaluator.h"

namespace {

constexpr int kFunctionPointerSizeBits = 64; // x64 target: always 8 bytes

// [dcl.ref]/5: no references to references, no arrays of references, and no
// pointers to references. The ordered spine records those shapes; reject them
// here instead of importing a type the language forbids.
bool orderedReferenceComponentIsIllFormed(
	std::span<const DeclaratorComponent> components) {
	for (size_t index = 0; index + 1 < components.size(); ++index) {
		const DeclaratorComponentKind outer = components[index].kind;
		const DeclaratorComponentKind inner = components[index + 1].kind;
		const bool inner_is_reference =
			inner == DeclaratorComponentKind::LValueReference ||
			inner == DeclaratorComponentKind::RValueReference;
		if (!inner_is_reference) {
			continue;
		}
		if (outer == DeclaratorComponentKind::Pointer ||
			outer == DeclaratorComponentKind::Array ||
			outer == DeclaratorComponentKind::UnknownBoundArray ||
			outer == DeclaratorComponentKind::LValueReference ||
			outer == DeclaratorComponentKind::RValueReference) {
			return true;
		}
	}
	return false;
}

// [dcl.fct]/1 forbids an array of functions and a function returning an array.
// The ordered spine records those shapes; reject them here instead of importing
// a type the canonical table cannot represent.
bool orderedFunctionComponentIsIllFormed(
	std::span<const DeclaratorComponent> components) {
	for (size_t index = 0; index + 1 < components.size(); ++index) {
		const DeclaratorComponentKind outer = components[index].kind;
		const DeclaratorComponentKind inner = components[index + 1].kind;
		const bool outer_is_array =
			outer == DeclaratorComponentKind::Array ||
			outer == DeclaratorComponentKind::UnknownBoundArray;
		const bool inner_is_array =
			inner == DeclaratorComponentKind::Array ||
			inner == DeclaratorComponentKind::UnknownBoundArray;
		if (outer_is_array && inner == DeclaratorComponentKind::Function) {
			return true;
		}
		if (outer == DeclaratorComponentKind::Function && inner_is_array) {
			return true;
		}
	}
	return false;
}

// The single-function projection path handles one Function component; the
// nested-callable path builds each inner signature onto the return
// FunctionType's callable_signature.
std::optional<size_t> singleFunctionComponentIndex(
	std::span<const DeclaratorComponent> components) {
	std::optional<size_t> found;
	for (size_t index = 0; index < components.size(); ++index) {
		if (components[index].kind != DeclaratorComponentKind::Function) {
			continue;
		}
		if (found.has_value()) {
			return std::nullopt;
		}
		found = index;
	}
	return found;
}

// [dcl.ambig.res]: `Receiver receiver(Source::answer);` is an object declaration
// when the qualified terminal names a non-type, and a function declaration only
// when that terminal names a type. Classification reads published member types
// and the class member tables; it does not invent a type from the spelling.
enum class QualifiedMemberKind : uint8_t {
	Type,
	Value,
	Ambiguous,
	Unknown
};

struct QualifiedMemberClassification {
	QualifiedMemberKind kind = QualifiedMemberKind::Unknown;
	const TypeInfo* type_info = nullptr;
	const TypeInfo* declaring_type = nullptr;
};

const TypeInfo* canonicalClassOrEnumType(const TypeInfo* type_info) {
	size_t remaining = getTypeInfoCount() + 1;
	while (type_info != nullptr && remaining-- > 0) {
		if (type_info->getStructInfo() != nullptr || type_info->getEnumInfo() != nullptr) {
			return type_info;
		}
		if (!type_info->isTypeAlias()) {
			return type_info;
		}
		const ResolvedAliasTypeInfo resolved = resolveAliasTypeInfo(
			type_info->registeredTypeIndex().withCategory(type_info->typeEnum()));
		const TypeInfo* next = tryGetTypeInfo(resolved.type_index);
		if (next == nullptr || next == type_info) {
			return type_info;
		}
		type_info = next;
	}
	return type_info;
}

bool isInjectedClassName(const TypeInfo& type_info, StringHandle member_name) {
	const std::string_view owner_name = StringTable::getStringView(type_info.name());
	const size_t separator = owner_name.rfind("::");
	const std::string_view unqualified =
		separator == std::string_view::npos ? owner_name : owner_name.substr(separator + 2);
	return unqualified == StringTable::getStringView(member_name);
}

const TypeInfo* lookupPublishedMemberType(const TypeInfo& owner, StringHandle member_name) {
	if (!owner.name().isValid() || !member_name.isValid()) {
		return nullptr;
	}
	const StringHandle key = buildQualifiedName(StringTable::getStringView(owner.name()), StringTable::getStringView(member_name));
	const auto found = getTypesByNameMap().find(key);
	if (found == getTypesByNameMap().end()) {
		return nullptr;
	}
	return found->second;
}

bool classDeclaresNonTypeMember(const StructTypeInfo& info, StringHandle member_name) {
	if (const StructMemberFunction* function = info.findMemberFunction(member_name);
		function != nullptr && !function->is_constructor && !function->is_destructor) {
		return true;
	}
	if (info.findStaticMember(member_name) != nullptr) {
		return true;
	}
	if (info.findMember(member_name) != nullptr) {
		return true;
	}
	for (const TypeIndex enum_index : info.getNestedEnumIndices()) {
		const TypeInfo* enum_type = tryGetTypeInfo(enum_index);
		const EnumTypeInfo* enum_info = enum_type != nullptr ? enum_type->getEnumInfo() : nullptr;
		if (enum_info != nullptr && !enum_info->is_scoped && enum_info->findEnumerator(member_name) != nullptr) {
			return true;
		}
	}
	return false;
}

QualifiedMemberClassification classifyDirectQualifiedMember(const TypeInfo& owner, StringHandle member_name) {
	QualifiedMemberClassification result;
	if (const EnumTypeInfo* enum_info = owner.getEnumInfo()) {
		if (enum_info->findEnumerator(member_name) != nullptr) {
			result.kind = QualifiedMemberKind::Value;
		}
		return result;
	}
	if (isInjectedClassName(owner, member_name)) {
		result.kind = QualifiedMemberKind::Type;
		result.type_info = &owner;
		return result;
	}
	if (const TypeInfo* published = lookupPublishedMemberType(owner, member_name)) {
		result.kind = QualifiedMemberKind::Type;
		result.type_info = published;
		return result;
	}
	if (const StructTypeInfo* info = owner.getStructInfo();
		info != nullptr && classDeclaresNonTypeMember(*info, member_name)) {
		result.kind = QualifiedMemberKind::Value;
	}
	return result;
}

template <typename BaseRecordList>
void appendDirectBaseRecords(const TypeInfo& record, BaseRecordList& bases) {
	const StructTypeInfo* info = record.getStructInfo();
	if (info == nullptr) {
		return;
	}
	auto append = [&](const BaseClassSpecifier& base) {
		const TypeInfo* base_record = canonicalClassOrEnumType(tryGetTypeInfo(base.type_index));
		if (base_record == nullptr) {
			return;
		}
		for (const TypeInfo* existing : bases) {
			if (existing == base_record) {
				return;
			}
		}
		bases.push_back(base_record);
	};
	for (const BaseClassSpecifier& base : info->base_classes) {
		append(base);
	}
	for (const BaseClassSpecifier& base : info->virtual_bases) {
		append(base);
	}
}

bool isRecordBaseOf(const TypeInfo* derived, const TypeInfo* ancestor) {
	if (derived == nullptr || ancestor == nullptr || derived == ancestor) {
		return false;
	}
	std::vector<const TypeInfo*> pending;
	std::vector<const TypeInfo*> seen;
	appendDirectBaseRecords(*derived, pending);
	while (!pending.empty()) {
		const TypeInfo* current = pending.back();
		pending.pop_back();
		bool already_seen = false;
		for (const TypeInfo* previous : seen) {
			if (previous == current) {
				already_seen = true;
				break;
			}
		}
		if (already_seen) {
			continue;
		}
		seen.push_back(current);
		if (current == ancestor) {
			return true;
		}
		appendDirectBaseRecords(*current, pending);
	}
	return false;
}

bool sameQualifiedDeclaration(const QualifiedMemberClassification& lhs, const QualifiedMemberClassification& rhs) {
	return lhs.kind == rhs.kind && lhs.declaring_type == rhs.declaring_type && lhs.type_info == rhs.type_info;
}

QualifiedMemberClassification mergeQualifiedLookup(std::span<const QualifiedMemberClassification> found) {
	FlashCpp::QualifiedLookupVector<QualifiedMemberClassification> merged;
	for (const QualifiedMemberClassification& candidate : found) {
		if (candidate.kind == QualifiedMemberKind::Unknown) {
			continue;
		}
		if (candidate.kind == QualifiedMemberKind::Ambiguous) {
			return candidate;
		}
		bool dominated = false;
		for (size_t index = 0; index < merged.size();) {
			if (sameQualifiedDeclaration(merged[index], candidate)) {
				dominated = true;
				break;
			}
			if (isRecordBaseOf(candidate.declaring_type, merged[index].declaring_type)) {
				merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(index));
				continue;
			}
			if (isRecordBaseOf(merged[index].declaring_type, candidate.declaring_type)) {
				dominated = true;
				break;
			}
			++index;
		}
		if (!dominated) {
			merged.push_back(candidate);
		}
	}
	if (merged.empty()) {
		return {};
	}
	if (merged.size() == 1) {
		return merged.front();
	}
	QualifiedMemberClassification ambiguous;
	ambiguous.kind = QualifiedMemberKind::Ambiguous;
	return ambiguous;
}

QualifiedMemberClassification classifyQualifiedMember(const TypeInfo* owner, StringHandle member_name) {
	struct LookupTask {
		const TypeInfo* record = nullptr;
		bool bases_pushed = false;
	};
	std::vector<LookupTask> stack;
	std::vector<const TypeInfo*> memo_records;
	std::vector<QualifiedMemberClassification> memo_results;
	auto find_memo = [&](const TypeInfo* record) -> const QualifiedMemberClassification* {
		for (size_t index = 0; index < memo_records.size(); ++index) {
			if (memo_records[index] == record) {
				return &memo_results[index];
			}
		}
		return nullptr;
	};
	auto remember = [&](const TypeInfo* record, QualifiedMemberClassification result) {
		memo_records.push_back(record);
		memo_results.push_back(result);
	};
	const TypeInfo* start = canonicalClassOrEnumType(owner);
	if (start == nullptr) {
		return {};
	}
	stack.push_back(LookupTask{start, false});
	while (!stack.empty()) {
		const size_t index = stack.size() - 1;
		const TypeInfo* record = stack[index].record;
		if (find_memo(record) != nullptr) {
			stack.pop_back();
			continue;
		}
		if (!stack[index].bases_pushed) {
			QualifiedMemberClassification direct = classifyDirectQualifiedMember(*record, member_name);
			if (direct.kind != QualifiedMemberKind::Unknown) {
				direct.declaring_type = record;
				remember(record, direct);
				stack.pop_back();
				continue;
			}
			stack[index].bases_pushed = true;
			FlashCpp::QualifiedLookupVector<const TypeInfo*> bases;
			appendDirectBaseRecords(*record, bases);
			size_t pushed = 0;
			for (const TypeInfo* base_record : bases) {
				if (find_memo(base_record) != nullptr) {
					continue;
				}
				bool on_stack = false;
				for (const LookupTask& task : stack) {
					if (task.record == base_record) {
						on_stack = true;
						break;
					}
				}
				if (on_stack) {
					continue;
				}
				stack.push_back(LookupTask{base_record, false});
				++pushed;
			}
			if (pushed == 0) {
				remember(record, QualifiedMemberClassification{});
				stack.pop_back();
			}
			continue;
		}
		FlashCpp::QualifiedLookupVector<const TypeInfo*> bases;
		FlashCpp::QualifiedLookupVector<QualifiedMemberClassification> found;
		appendDirectBaseRecords(*record, bases);
		for (const TypeInfo* base_record : bases) {
			if (const QualifiedMemberClassification* base_result = find_memo(base_record)) {
				found.push_back(*base_result);
			}
		}
		remember(record, mergeQualifiedLookup(found));
		stack.pop_back();
	}
	if (const QualifiedMemberClassification* result = find_memo(start)) {
		return *result;
	}
	return {};
}

}

Parser::MemberPointerOwnerParse Parser::parseMemberPointerOwnerAfterName(
	Token class_name_token) {
	MemberPointerOwnerParse parsed;
	parsed.spelling = class_name_token.handle();
	parsed.owner_token = class_name_token;
	if (peek() != "<"_tok) {
		return parsed;
	}
	const std::string_view primary = class_name_token.value();
	const auto class_template = gTemplateRegistry.lookupTemplate(primary);
	if (!class_template.has_value() ||
		!class_template->is<TemplateClassDeclarationNode>()) {
		return parsed;
	}
	SaveHandle args_pos = save_token_position();
	std::vector<ASTNode> argument_syntax_nodes;
	auto args = parse_explicit_template_arguments(class_template->as<TemplateClassDeclarationNode>().template_parameters(), &argument_syntax_nodes);
	if (!args.has_value()) {
		restore_token_position(args_pos);
		return parsed;
	}
	discard_saved_token(args_pos);
	TypeSpecifierNode owner_type_spec(TypeCategory::Struct, TypeQualifier::None, SizeInBits{0}, class_name_token, CVQualifier::None);
	tryStampTypeOnlyClassTemplateSpecialization(owner_type_spec, class_name_token.handle(), *args, argument_syntax_nodes);
	if (owner_type_spec.has_template_specialization()) {
		CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
		CanonicalTypeTransaction transaction(table);
		const CanonicalTypeImport imported = importCanonicalType(table, owner_type_spec);
		if (imported.status == CanonicalTypeImportStatus::Supported && table.node(imported.type).kind == CanonicalTypeKind::TemplateSpecialization) {
			parsed.specialization_type_id = imported.type;
			transaction.commit();
		}
	}
	if (auto instantiated = try_instantiate_class_template(primary, *args);
		instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
		registerAndNormalizeLateMaterializedTopLevelNode(*instantiated);
		parsed.specialization_node = *instantiated;
	}
	parsed.spelling = StringTable::getOrInternStringHandle(
		get_instantiated_class_name(primary, *args));
	return parsed;
}

void Parser::applyMemberPointerOwner(
	TypeSpecifierNode& type_spec,
	const MemberPointerOwnerParse& parsed) {
	// Lexical projection: has_member_class() and legacy consumers still read
	// the spelling, but specialization owners publish TypeId as authority.
	type_spec.set_member_class_name(parsed.spelling);

	if (parsed.specialization_node.has_value() &&
		parsed.specialization_node->is<StructDeclarationNode>()) {
		CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
		CanonicalTypeTransaction transaction(table);
		const CanonicalTypeImport imported = importCanonicalClassDeclaration(
			table, parsed.specialization_node->as<StructDeclarationNode>());
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			const CanonicalTypeKind kind = table.node(imported.type).kind;
			if (kind == CanonicalTypeKind::TemplateSpecialization ||
				kind == CanonicalTypeKind::Record) {
				type_spec.set_member_class_type_id(imported.type);
				const auto type_it = getTypesByNameMap().find(parsed.spelling);
				if (type_it != getTypesByNameMap().end() &&
					type_it->second != nullptr) {
					if (const StructTypeInfo* struct_info =
							canonicalClassStructInfoFromTypeInfo(*type_it->second)) {
						(void)tryPublishCanonicalClassBaseSchema(
							table, imported.type, *struct_info);
					}
				}
				transaction.commit();
				return;
			}
		}
	}

	if (parsed.specialization_type_id) {
		type_spec.set_member_class_type_id(parsed.specialization_type_id);
		return;
	}

	tryBindPublishedMemberClassEntity(type_spec);

	// A member-pointer owner must name a class or union. An alias that resolves to
	// a non-class type (for example `using Int = int; int Int::* p;`) would
	// otherwise bind its resolved type as the owner and reach canonical
	// member-pointer construction, which fails internally instead of rejecting the
	// declaration.
	if (type_spec.has_member_class_type_id()) {
		CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
		const CanonicalTypeKind owner_kind = table.node(type_spec.member_class_type_id()).kind;
		if (owner_kind != CanonicalTypeKind::Record &&
			owner_kind != CanonicalTypeKind::TemplateSpecialization) {
			throw makeStructuredCompileError(
				context_.diagnostics(),
				DiagnosticId::MemberPointerOwnerNotClass,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(parsed.owner_token),
				"member pointer owner must be a class or union type",
				{});
		}
	}
}

void Parser::bindMemberPointerOwner(TypeSpecifierNode& type_spec, Token class_name_token) {
	applyMemberPointerOwner(type_spec, parseMemberPointerOwnerAfterName(class_name_token));
}

ParseResult Parser::parse_type_and_name(CVQualifier leading_cv_qualifier) {
	// Add parsing depth check to prevent infinite loops
	if (++parsing_depth_ > MAX_PARSING_DEPTH) {
		--parsing_depth_;
		FLASH_LOG(Parser, Error, "Maximum parsing depth (", MAX_PARSING_DEPTH, ") exceeded in parse_type_and_name()");
		FLASH_LOG(Parser, Error, "This indicates an infinite loop in type parsing");
		return ParseResult::error("Maximum parsing depth exceeded - possible infinite loop", current_token_);
	}

	// RAII guard to decrement depth on all exit paths
	struct DepthGuard {
		size_t& depth;
		~DepthGuard() { --depth; }
	} depth_guard{parsing_depth_};

	FLASH_LOG(Parser, Debug, "parse_type_and_name: Starting, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Check for alignas specifier before the type
	std::optional<size_t> custom_alignment = parse_alignas_specifier();

	// Parse the type specifier
	FLASH_LOG(Parser, Debug, "parse_type_and_name: About to parse type_specifier, current token: ", !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
	ParseResult type_specifier_result;
	{
		FlashCpp::ScopedState implicit_typename_guard(parsing_implicit_typename_context_);
		parsing_implicit_typename_context_ = true;
		type_specifier_result = parse_type_specifier();
	}
	if (type_specifier_result.is_error()) {
		FLASH_LOG(Parser, Debug, "parse_type_and_name: parse_type_specifier failed: ", type_specifier_result.error_message());
		return type_specifier_result;
	}

	if (!type_specifier_result.node().has_value()) {
		return ParseResult::error("Expected type specifier", current_token_);
	}

	// Get the type specifier node to modify it with pointer levels
	TypeSpecifierNode& type_spec = type_specifier_result.node()->as<TypeSpecifierNode>();
	type_spec.add_cv_qualifier(leading_cv_qualifier);

	// Check for structured binding: auto [a, b, c] = expr;
	// Also support: auto& [a, b] = pair; and auto&& [x, y] = temp;
	// Per C++20 [dcl.struct.bind], decltype(auto) is not permitted in structured bindings.
	// This must be checked after parsing the type specifier (auto) but before parsing pointer/reference/identifier
	if (type_spec.category() == TypeCategory::DeclTypeAuto) {
		if (type_spec.cv_qualifier() != CVQualifier::None) {
			context_.diagnostics().report(
				DiagnosticId::DecltypeAutoCvQualifier,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(current_token_),
				"'decltype(auto)' cannot have cv-qualifiers",
				{});
			return ParseResult::error("'decltype(auto)' cannot have cv-qualifiers", current_token_);
		}

		if (peek() == "*"_tok || peek() == "&"_tok || peek() == "&&"_tok) {
			context_.diagnostics().report(
				DiagnosticId::DecltypeAutoPointerOrReference,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(current_token_),
				"'decltype(auto)' cannot be combined with pointer or reference declarators",
				{});
			return ParseResult::error("'decltype(auto)' cannot be combined with pointer or reference declarators", current_token_);
		}

		// Peek ahead to see if '[' follows. Per C++20 [dcl.struct.bind], decltype(auto)
		// is not permitted in structured bindings.
		SaveHandle sb_check = save_token_position();
		bool is_structured_binding = (peek() == "["_tok);
		restore_token_position(sb_check);
		if (is_structured_binding) {
			context_.diagnostics().report(
				DiagnosticId::DecltypeAutoStructuredBinding,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(current_token_),
				"'decltype(auto)' cannot be used in structured bindings",
				{});
			return ParseResult::error("'decltype(auto)' cannot be used in structured bindings", current_token_);
		}
	}
	if (type_spec.category() == TypeCategory::Auto) {
		// Check for optional reference qualifiers
		ReferenceQualifier ref_qualifier = ReferenceQualifier::None;

		if (peek() == "&"_tok) {
			advance(); // consume '&'

			// Check if it's && (rvalue reference) or just & (lvalue reference)
			if (peek() == "&"_tok) {
				advance(); // consume second '&'
				ref_qualifier = ReferenceQualifier::RValueReference;
			} else {
				ref_qualifier = ReferenceQualifier::LValueReference;
			}
		}

		// Now check for '[' to confirm structured binding
		if (peek() == "["_tok) {
			FLASH_LOG(Parser, Debug, "parse_type_and_name: Detected structured binding pattern: auto [");

			// Parse structured binding with reference qualifier
			return parse_structured_binding(type_spec.cv_qualifier(), ref_qualifier);
		}

		// If we consumed reference qualifiers but it's not a structured binding,
		// apply them to the type_spec (e.g., auto& ref = x; or auto&& rvalue = temp;)
		if (ref_qualifier != ReferenceQualifier::None) {
			type_spec.set_reference_qualifier(ref_qualifier);
		}
	}

	// C++20 constrained auto parameters: ConceptName auto param or Concept<T> auto param
	// The concept constraint was parsed as a UserDefined type by parse_type_specifier().
	// If followed by 'auto', this is an abbreviated function template parameter.
	// Store the concept name on the TypeSpecifierNode for requires clause generation.
	if (type_spec.category() == TypeCategory::UserDefined && peek() == "auto"_tok) {
		// Capture the concept name before converting the type to Auto
		std::string_view concept_name = type_spec.token().value();
		FLASH_LOG(Parser, Debug, "parse_type_and_name: Constrained auto parameter detected (concept='", concept_name, "'), consuming 'auto'");
		advance(); // consume 'auto'
		type_spec.set_category(TypeCategory::Auto);
		// Store the concept constraint so abbreviated template generation can build a requires clause
		type_spec.set_concept_constraint(concept_name);
	}

	skip_noop_gnu_qualifiers();

	// Extract calling convention specifiers that can appear after the type
	// Example: void __cdecl func(); or int __stdcall* func();
	// We consume them here and save to last_calling_convention_ for the caller to retrieve
	last_calling_convention_ = CallingConvention::Default;
	while (peek().is_identifier()) {
		std::string_view token_val = peek_info().value();

		// Look up calling convention in the mapping table using ranges
		auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
		if (it != std::end(calling_convention_map)) {
			last_calling_convention_ = it->convention;
			advance();  // Consume calling convention token
		} else {
			break;  // Not a calling convention keyword, stop scanning
		}
	}

	skip_noop_gnu_qualifiers();

	// Regular pointer/reference/identifier parsing (existing code)
	// Check for pointer-to-member syntax: ClassName::* or ClassTemplate<Args>::*
	// Pattern: int Point::*ptr_to_x
	if (peek().is_identifier()) {
		// Save position in case this isn't a pointer-to-member
		SaveHandle saved_pos = save_token_position();
		Token class_name_token = peek_info();
		advance(); // consume class name
		const MemberPointerOwnerParse owner =
			parseMemberPointerOwnerAfterName(class_name_token);

		// Check for ::
		if (peek() == "::"_tok) {
			advance(); // consume '::'

			// Check for *
			if (peek() == "*"_tok) {
				advance(); // consume '*'

				// This is a pointer-to-member declaration!
				FLASH_LOG(Parser, Debug, "parse_type_and_name: Detected pointer-to-member: ",
						  class_name_token.value(), "::*");

				applyMemberPointerOwner(type_spec, owner);

				// Add a pointer level to indicate this is a pointer
				CVQualifier member_pointer_cv = parse_cv_qualifiers();
				skip_noop_gnu_qualifiers();
				member_pointer_cv |= parse_cv_qualifiers();
				type_spec.add_pointer_level(member_pointer_cv);

				// Discard the saved position and continue parsing
				discard_saved_token(saved_pos);
			} else {
				// Not a pointer-to-member, restore position
				restore_token_position(saved_pos);
			}
		} else {
			// Not followed by ::, restore position
			restore_token_position(saved_pos);
		}
	}

	// Parse pointer declarators: * [const] [volatile] *...
	// Example: int* const* volatile ptr
	while (peek() == "*"_tok) {
		advance(); // consume '*'

		// Check for CV-qualifiers after the *
		CVQualifier ptr_cv = parse_cv_qualifiers();
		skip_noop_gnu_qualifiers();
		ptr_cv |= parse_cv_qualifiers();

		if (type_spec.is_reference() || type_spec.is_rvalue_reference()) {
			const std::string message = "Cannot form a pointer to reference type";
			context_.diagnostics().report(
				DiagnosticId::PointerToReferenceType,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(current_token_),
				message,
				{});
			return ParseResult::error(message, current_token_);
		}
		type_spec.add_pointer_level(ptr_cv);
	}

	// Parse postfix cv-qualifiers before pointers/references: Type const* or Type const&
	// This handles C++ postfix const/volatile syntax like: typename _Str::value_type const*
	CVQualifier postfix_cv = parse_cv_qualifiers();
	type_spec.add_cv_qualifier(postfix_cv);
	skip_noop_gnu_qualifiers();

	// After postfix cv-qualifiers, parse pointer and reference declarators.
	// This handles patterns like: typename _Str::value_type const* __lhs
	// where const appears between the dependent type and the pointer.
	consume_pointer_ref_modifiers(type_spec);

	// Check for calling convention AFTER pointer/reference declarators
	// Example: void* __cdecl func(); or int& __stdcall func();
	// This handles the case where calling convention appears after * or &
	while (peek().is_identifier()) {
		std::string_view token_val = peek_info().value();

		// Look up calling convention in the mapping table using ranges
		auto it = std::ranges::find(calling_convention_map, token_val, &CallingConventionMapping::keyword);
		if (it != std::end(calling_convention_map)) {
			last_calling_convention_ = it->convention;
			advance();  // Consume calling convention token
		} else {
			break;  // Not a calling convention keyword, stop scanning
		}
	}

	// [dcl.decl]: after the decl-specifier-seq, one declarator owns every
	// parenthesized ptr-declarator. A failure is not retried as an identifier.
	if (peek() == "("_tok) {
		// Reference-to-array: T (&name)[N][M]...
		Token ref_identifier;
		bool has_name = false;
		std::vector<ASTNode> ref_array_dimensions;
		if (tryConsumeReferenceToArrayDeclarator(type_spec, &ref_identifier, &has_name, &ref_array_dimensions)) {
			if (!has_name) {
				ref_identifier = Token(Token::Type::Identifier, ""sv, type_spec.token().line(), type_spec.token().column(), type_spec.token().file_index());
			}
			ASTNode ref_array_decl = emplace_node<DeclarationNode>(
				emplace_node<TypeSpecifierNode>(type_spec), ref_identifier, std::move(ref_array_dimensions));
			if (custom_alignment.has_value()) {
				ref_array_decl.as<DeclarationNode>().set_custom_alignment(*custom_alignment);
			}
			return ParseResult::success(ref_array_decl);
		}
		ParseResult declarator_result = parse_declarator(type_spec, Linkage::None);
		if (declarator_result.is_error() || !declarator_result.node().has_value()) {
			return declarator_result.is_error() ? std::move(declarator_result) : ParseResult::error("Expected declarator", current_token_);
		}
		if (custom_alignment.has_value()) {
			std::optional<ASTNode> declarator_node = declarator_result.node();
			if (DeclarationNode* declaration = declarator_node->get_if<DeclarationNode>()) {
				declaration->set_custom_alignment(*custom_alignment);
			} else if (FunctionDeclarationNode* function = declarator_node->get_if<FunctionDeclarationNode>()) {
				function->decl_node().set_custom_alignment(*custom_alignment);
			}
		}
		return declarator_result;
	}

	// Check for parameter pack: Type... identifier
	// This is used in variadic function templates like: template<typename... Args> void func(Args... args)
	bool is_parameter_pack = false;
	if (!peek().is_eof() &&
		(peek_info().type() == Token::Type::Operator || peek_info().type() == Token::Type::Punctuator) &&
		peek() == "..."_tok) {
		// When parsing out-of-line member template definitions, a pack expansion can
		// remain at the end of a template-id in a parameter type:
		//   Type<Args...>, ...
		// In that case this ellipsis belongs to the type, not to a named parameter pack.
		if (peek(1) == ">"_tok || peek(1) == ">>"_tok) {
			advance(); // consume type pack expansion suffix
		} else {
			advance(); // consume '...'
			is_parameter_pack = true;
		}
	}

	// Check for alignas specifier before the identifier (if not already specified)
	if (!custom_alignment.has_value()) {
		custom_alignment = parse_alignas_specifier();
	}

	// Parse the identifier (name) or operator overload
	Token identifier_token;
	bool has_noreturn_attribute = false;

	// Check for operator overload (e.g., operator=, operator())
	if (peek() == "operator"_tok) {

		Token operator_keyword_token = peek_info();
		advance(); // consume 'operator'

		std::string_view operator_name;

		// Use the shared helper to parse the operator name
		if (auto err = parse_operator_name(operator_keyword_token, operator_name)) {
			return std::move(*err);
		}

		// Create a synthetic identifier token for the operator
		identifier_token = Token(Token::Type::Identifier, operator_name,
								 operator_keyword_token.line(), operator_keyword_token.column(),
								 operator_keyword_token.file_index());

		// Skip any C++ attributes like [[nodiscard]] that may appear after the operator name
		// but before the parameter list (e.g., operator() [[nodiscard]] (args))
		has_noreturn_attribute = skip_cpp_attributes_with_info().has_noreturn;
	} else {
		// Regular identifier (or unnamed parameter)
		// First, skip any specifiers that may appear after the return type but before the identifier
		// This handles non-standard (but valid in GCC/libstdc++) patterns like: void constexpr operator=()
		while (peek().is_keyword()) {
			std::string_view kw = peek_info().value();
			if (kw == "constexpr" || kw == "consteval" || kw == "inline") {
				advance(); // skip the specifier
			} else {
				break;
			}
		}

		// Skip GCC __attribute__((...)) that may appear between return type and function name
		// e.g., inline _Atomic_word __attribute__((__always_inline__)) __exchange_and_add(...)
		skip_gcc_attributes();

		// After skipping specifiers, check if this is now an operator (e.g., void constexpr operator=())
		if (peek() == "operator"_tok) {
			Token operator_keyword_token = peek_info();
			advance(); // consume 'operator'

			std::string_view operator_name;

			// Use the shared helper to parse the operator name
			if (auto err = parse_operator_name(operator_keyword_token, operator_name)) {
				return std::move(*err);
			}

			// Create a synthetic identifier token for the operator
			identifier_token = Token(Token::Type::Identifier, operator_name,
									 operator_keyword_token.line(), operator_keyword_token.column(),
									 operator_keyword_token.file_index());
		} else {
			// Check if this might be an unnamed parameter (next token is ',', ')', '=', or '[')
			FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Parsing identifier. current_token={}, peek={}",
							 std::string(current_token_.value()),
							 !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
			if (!peek().is_eof()) {
				auto next = peek_info().value();
				if (next == "," || next == ")" || next == "=" || next == "[" ||
					next == ">" || next == ">>" ||
					next == ":" || next == ";") {
					// This is an unnamed parameter/member - create a synthetic empty identifier
					// ':' handles unnamed bitfields (e.g., int :32;) and ';' handles unnamed members
					FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Unnamed parameter detected, next={}", std::string(next));
					identifier_token = Token(Token::Type::Identifier, ""sv,
											 current_token_.line(), current_token_.column(),
											 current_token_.file_index());
				} else {
					// Regular identifier
					FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Consuming token as identifier, peek={}", std::string(next));
					auto id_token = advance();
					if (id_token.type() != Token::Type::Identifier) {
						return ParseResult::error("Expected identifier token", id_token);
					}
					identifier_token = id_token;
					FLASH_LOG_FORMAT(Parser, Debug, "parse_type_and_name: Consumed identifier={}, now current_token={}, peek={}",
									 std::string(identifier_token.value()),
									 std::string(current_token_.value()),
									 !peek().is_eof() ? std::string(peek_info().value()) : "N/A");
				}
			} else {
				return ParseResult::error("Expected identifier or end of parameter", Token());
			}
		}
	}

	// Preserve function attributes between the declarator-id and parameter list.
	has_noreturn_attribute |= skip_cpp_attributes_with_info().has_noreturn;

	// Check for array declaration: identifier[size] or identifier[size1][size2]...
	std::vector<ASTNode> array_dimensions;
	bool is_unsized_array = false;
	while (peek() == "["_tok) {
		advance(); // consume '['

		// Check for empty brackets (unsized array, size inferred from initializer)
		// Only the first dimension can be unsized
		if (peek() == "]"_tok) {
			if (array_dimensions.empty()) {
				// Empty brackets - array size will be inferred from initializer
				is_unsized_array = true;
			} else {
				return ParseResult::error("Only the first array dimension can be unsized", current_token_);
			}
		} else {
			// Parse the array size expression
			ParseResult size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}
			array_dimensions.push_back(*size_result.node());
		}

		// Expect closing ']'
		if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator ||
			peek() != "]"_tok) {
			return ParseResult::error("Expected ']' after array size", current_token_);
		}
		advance(); // consume ']'
	}

	std::optional<std::string_view> asm_symbol_name;
	skip_asm_suffix(&asm_symbol_name);

	// An alias whose target is an array (`using A = int[3];`) carries its extents
	// on the type specifier rather than in declarator brackets. Mirror known
	// extents into the declaration's array dimensions so array-object handling,
	// initializer sizing, and sizeof agree with a directly written `int a[3];`.
	if (!parsing_parameter_declaration_type_id_ &&
		array_dimensions.empty() && !is_unsized_array) {
		if (auto type_node = type_specifier_result.node()) {
			const TypeSpecifierNode& spec = type_node->as<TypeSpecifierNode>();
			bool known_extents = spec.is_array() && !spec.has_pointee_array_declarator() &&
				!spec.array_dimensions().empty() &&
				!spec.has_unsized_outer_array_dimension();
			for (const size_t extent : spec.array_dimensions()) {
				if (extent == 0) {
					known_extents = false;
					break;
				}
			}
			if (known_extents) {
				for (const size_t extent : spec.array_dimensions()) {
					const Token extent_token(Token::Type::Literal, std::string_view{},
						identifier_token.line(), identifier_token.column(),
						identifier_token.file_index());
					array_dimensions.push_back(ASTNode::emplace_node<ExpressionNode>(
						NumericLiteralNode(extent_token,
							static_cast<unsigned long long>(extent),
							TypeCategory::UnsignedLongLong, TypeQualifier::None, 64)));
				}
			}
		}
	}

	// Unwrap the optional ASTNode before passing it to emplace_node
	if (auto node = type_specifier_result.node()) {
		ASTNode decl_node;
		if (!array_dimensions.empty()) {
			decl_node = emplace_node<DeclarationNode>(*node, identifier_token, std::move(array_dimensions));
			if (is_unsized_array) {
				decl_node.as<DeclarationNode>().set_unsized_array(true);
			}
		} else if (is_unsized_array) {
			// Mark as an unsized array - size will be inferred from initializer
			decl_node = emplace_node<DeclarationNode>(*node, identifier_token);
			decl_node.as<DeclarationNode>().set_unsized_array(true);
		} else {
			decl_node = emplace_node<DeclarationNode>(*node, identifier_token);
		}

		// Apply custom alignment if specified
		if (custom_alignment.has_value()) {
			decl_node.as<DeclarationNode>().set_custom_alignment(custom_alignment.value());
		}
		if (has_noreturn_attribute) {
			decl_node.as<DeclarationNode>().set_noreturn_attribute(true);
		}
		if (asm_symbol_name.has_value()) {
			decl_node.as<DeclarationNode>().set_mangled_name(*asm_symbol_name);
		}

		// Apply parameter pack flag if this is a parameter pack
		if (is_parameter_pack) {
			decl_node.as<DeclarationNode>().set_parameter_pack(true);
		}

		return ParseResult::success(decl_node);
	}
	return ParseResult::error("Invalid type specifier node", identifier_token);
}

void Parser::parse_variable_declarator_suffixes(DeclarationNode& decl) {
	std::optional<std::string_view> asm_symbol_name;
	skip_cpp_attributes();
	while (true) {
		std::optional<std::string_view> current_asm_symbol_name;
		if (!skip_asm_suffix(&current_asm_symbol_name)) {
			break;
		}
		// parse_postfix_declarator captures a first __asm suffix into the
		// declaration itself; any further suffix here is therefore already the
		// second one even when this loop has not captured one yet.
		if (asm_symbol_name.has_value() || decl.has_mangled_name()) {
			const Token& declarator_identifier = decl.identifier_token();
			const std::array<DiagnosticArgument, 1> arguments{
				DiagnosticArgument::text(declarator_identifier.value())};
			throw makeStructuredCompileError(
				context_.diagnostics(),
				DiagnosticId::MultipleAsmSuffixesOnDeclarator,
				DiagnosticSeverity::Error,
				lexer_.getSourceLocation(declarator_identifier),
				"Multiple __asm suffixes on declarator '{}' are not supported",
				arguments);
		}
		asm_symbol_name = current_asm_symbol_name;
		skip_cpp_attributes();
	}
	if (asm_symbol_name.has_value()) {
		decl.set_mangled_name(*asm_symbol_name);
	}
}

// Parse structured binding: auto [a, b, c] = expr;
// Also supports: auto& [a, b] = pair; and auto&& [x, y] = temp;
ParseResult Parser::parse_structured_binding(CVQualifier cv_qualifiers, ReferenceQualifier ref_qualifier) {
	FLASH_LOG(Parser, Debug, "parse_structured_binding: Starting");

	// At this point, we've already parsed 'auto' (and optional &/&&) and confirmed the next token is '['
	// Consume the '['
	if (peek() != "["_tok) {
		return ParseResult::error("Expected '[' for structured binding", current_token_);
	}
	const Token bracket_token = peek_info();
	advance(); // consume '['

	// Parse the identifier list: a, b, c
	std::vector<StringHandle> identifiers;

	while (true) {
		// Expect an identifier
		if (!peek().is_identifier()) {
			return ParseResult::error("Expected identifier in structured binding", current_token_);
		}

		Token id_token = peek_info();
		StringHandle id_handle = StringTable::createStringHandle(id_token.value());
		identifiers.push_back(id_handle);
		advance(); // consume identifier

		FLASH_LOG(Parser, Debug, "parse_structured_binding: Parsed identifier: ", StringTable::getStringView(id_handle));

		// Check for comma (more identifiers) or closing bracket
		if (peek() == ","_tok) {
			advance(); // consume ','
			// Continue to parse next identifier
		} else if (peek() == "]"_tok) {
			// End of identifier list
			break;
		} else {
			return ParseResult::error("Expected ',' or ']' in structured binding identifier list", current_token_);
		}
	}

	// Consume the ']'
	if (peek() != "]"_tok) {
		return ParseResult::error("Expected ']' after structured binding identifiers", current_token_);
	}
	advance(); // consume ']'

	FLASH_LOG(Parser, Debug, "parse_structured_binding: Parsed ", identifiers.size(), " identifiers");

	// Now expect the initializer: '=' expr or '{' expr '}' or '(' expr ')'
	// For C++17, we support '=' and '{}'
	// C++20 adds '()' but let's keep it simple for now
	if (peek().is_eof()) {
		return ParseResult::error("Expected initializer after structured binding identifiers", current_token_);
	}

	std::optional<ASTNode> initializer;

	if (peek() == "="_tok) {
		advance(); // consume '='

		// Parse the initializer expression
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_result.is_error()) {
			return init_result;
		}
		initializer = init_result.node();

	} else if (peek() == "{"_tok) {
		// Brace initializer: auto [a, b] {expr};
		// For now, we'll parse this as an expression that starts with '{'
		auto init_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
		if (init_result.is_error()) {
			return init_result;
		}
		initializer = init_result.node();

	} else {
		return ParseResult::error("Expected '=' or '{' after structured binding identifiers", current_token_);
	}

	if (!initializer.has_value()) {
		return ParseResult::error("Failed to parse structured binding initializer", current_token_);
	}

	FLASH_LOG(Parser, Debug, "parse_structured_binding: Successfully parsed initializer");

	// Create the StructuredBindingNode
	ASTNode binding_node = emplace_node<StructuredBindingNode>(
		std::move(identifiers),
		*initializer,
		cv_qualifiers,
		ref_qualifier,
		bracket_token);

	FLASH_LOG(Parser, Debug, "parse_structured_binding: Created StructuredBindingNode");

	// IMPORTANT: We need to add placeholder declarations to the symbol table for each identifier
	// so that the parser can find them when they're used later in the same scope.
	// The actual types will be determined during code generation, but we need placeholders for parsing.
	const StructuredBindingNode& sb_node = binding_node.as<StructuredBindingNode>();
	for (const auto& id_handle : sb_node.identifiers()) {
		std::string_view id_name = StringTable::getStringView(id_handle);

		// Create a placeholder TypeSpecifierNode (we'll use Auto type as a placeholder)
		TypeSpecifierNode placeholder_type(TypeCategory::Auto, TypeQualifier::None, 0, Token(), CVQualifier::None);

		// Create a placeholder DeclarationNode
		Token placeholder_token(Token::Type::Identifier, id_name, 0, 0, 0);
		ASTNode placeholder_decl = emplace_node<DeclarationNode>(
			emplace_node<TypeSpecifierNode>(placeholder_type),
			placeholder_token);

		// Add to symbol table
		if (!gSymbolTable.insert(id_name, placeholder_decl)) {
			FLASH_LOG(Parser, Warning, "Structured binding identifier '", id_name, "' already exists in scope");
		} else {
			FLASH_LOG(Parser, Debug, "parse_structured_binding: Added placeholder for '", id_name, "' to symbol table");
		}
	}

	return ParseResult::success(binding_node);
}

// Constant-folds parsed "[expr]" array bounds and applies them to a
// type-specifier ([dcl.array]/1).  The bounds are applied only when every
// expression folds to a positive constant, so a dependent bound never leaves a
// partially resolved dimension list behind for the semantic layer, which can
// re-resolve the full set later from the declaration's expressions.
void Parser::addConstantArrayDimensionsToTypeSpec(
	TypeSpecifierNode& type_spec,
	std::span<const ASTNode> dimension_exprs) {
	std::vector<size_t> dimensions;
	dimensions.reserve(dimension_exprs.size());
	for (const ASTNode& dimension_expr : dimension_exprs) {
		const auto dim_val = try_evaluate_constant_expression(dimension_expr);
		if (!dim_val.has_value() || dim_val->value <= 0) {
			return;
		}
		dimensions.push_back(static_cast<size_t>(dim_val->value));
	}
	for (const size_t dim : dimensions) {
		type_spec.add_array_dimension(dim);
	}
}

bool Parser::parseReferenceToArrayDeclarator(
	TypeSpecifierNode& type_spec,
	Token& out_identifier,
	bool& out_has_identifier,
	std::vector<ASTNode>& out_array_dimensions) {
	SaveHandle group_body_start = save_token_position();
	if (peek() != "&"_tok && peek() != "&&"_tok) {
		return false;
	}
	const bool is_rvalue_ref = (peek() == "&&"_tok);
	advance(); // consume '&' or '&&'

	Token identifier;
	bool has_identifier = false;
	if (peek().is_identifier()) {
		identifier = peek_info();
		has_identifier = true;
		advance();
	}

	// Reference-to-array only. A following '(' is a reference-to-function and
	// stays on the existing declarator path.
	if (peek() != ")"_tok) {
		restore_token_position(group_body_start);
		return false;
	}
	advance(); // consume ')'
	if (peek() != "["_tok) {
		restore_token_position(group_body_start);
		return false;
	}

	std::vector<ASTNode> array_dimensions;
	bool is_unsized_outer_array = false;
	while (peek() == "["_tok) {
		advance(); // consume '['

		// [dcl.array]/1: a reference to an array of unknown bound is valid
		// (`int(&)[]`), so the outermost extent may be absent. Only the first
		// dimension can be unsized.
		if (peek() == "]"_tok) {
			if (is_unsized_outer_array || !array_dimensions.empty()) {
				restore_token_position(group_body_start);
				return false;
			}
			is_unsized_outer_array = true;
		} else {
			auto size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				restore_token_position(group_body_start);
				return false;
			}
			array_dimensions.push_back(*size_result.node());
		}

		if (!consume("]"_tok)) {
			restore_token_position(group_body_start);
			return false;
		}
	}

	type_spec.set_reference_qualifier(
		is_rvalue_ref ? ReferenceQualifier::RValueReference : ReferenceQualifier::LValueReference);
	type_spec.set_array(true);
	addConstantArrayDimensionsToTypeSpec(type_spec, array_dimensions);
	if (is_unsized_outer_array) {
		type_spec.set_unsized_outer_array_dimension(true);
	}

	out_identifier = identifier;
	out_has_identifier = has_identifier;
	out_array_dimensions = std::move(array_dimensions);
	discard_saved_token(group_body_start);
	return true;
}

bool Parser::tryConsumeReferenceToArrayDeclarator(
	TypeSpecifierNode& type_spec,
	Token* out_identifier,
	bool* out_has_identifier,
	std::vector<ASTNode>* out_array_dimensions) {
	if (peek() != "("_tok) {
		return false;
	}
	SaveHandle group_start = save_token_position();
	advance(); // consume '('
	(void)parse_calling_convention(CallingConvention::Default);
	if (peek() != "&"_tok && peek() != "&&"_tok) {
		restore_token_position(group_start);
		return false;
	}

	Token identifier;
	bool has_identifier = false;
	std::vector<ASTNode> array_dimensions;
	if (!parseReferenceToArrayDeclarator(type_spec, identifier, has_identifier, array_dimensions)) {
		restore_token_position(group_start);
		return false;
	}

	if (out_identifier != nullptr) {
		*out_identifier = identifier;
	}
	if (out_has_identifier != nullptr) {
		*out_has_identifier = has_identifier;
	}
	if (out_array_dimensions != nullptr) {
		*out_array_dimensions = std::move(array_dimensions);
	}
	discard_saved_token(group_start);
	return true;
}

// NEW: Parse declarators (handles function pointers, arrays, etc.)
ParseResult Parser::parse_declarator(TypeSpecifierNode& base_type, Linkage linkage) {
	return parse_declarator(base_type, linkage, nullptr);
}

ParseResult Parser::parse_declarator(
	TypeSpecifierNode& base_type,
	Linkage linkage,
	std::vector<ASTNode>* array_bound_expressions) {
	auto tryStructuralPointerArrayDeclarator =
			[&]() -> std::optional<ParseResult> {
		// The recursive declarator grammar owns every parenthesized group.
		// A group that does not match is an error; it is not retried as a
		// flat identifier declarator.
		if (peek() == "("_tok) {
			SaveHandle structural_start = save_token_position();
			{
				// A Function component's parameter list and qualifiers, in
				// outermost-first order across the whole declarator. A nested callable
				// return (`int (*(*p)())()`) has two, so each Function component needs
				// its own signature.
				struct OrderedFunctionParams {
					FlashCpp::ParsedParameterList parameters;
					FlashCpp::MemberQualifiers qualifiers;
					FlashCpp::FunctionSpecifiers specifiers;
					CallingConvention calling_convention = CallingConvention::Default;
				};
				struct DeclaratorFrame {
					std::vector<DeclaratorComponent> prefixes;
					std::vector<DeclaratorComponent> child;
					std::vector<DeclaratorComponent> suffixes;
					std::vector<ASTNode> child_array_bound_expressions;
					std::vector<ASTNode> suffix_array_bound_expressions;
					std::vector<OrderedFunctionParams> child_functions;
					std::vector<OrderedFunctionParams> suffix_functions;
					CallingConvention calling_convention =
						CallingConvention::Default;
					bool delimited = false;
					bool after_direct = false;
				};
				std::vector<DeclaratorFrame> frames;
				frames.push_back({});
				if (base_type.has_member_class()) {
					frames.front().prefixes.push_back(
						base_type.has_member_class_type_id()
							? DeclaratorComponent::memberPointer(
								base_type.member_class_type_id(), false,
								base_type.pointer_levels().empty()
									? CVQualifier::None
									: base_type.pointer_levels().front().cv_qualifier)
							: DeclaratorComponent::memberPointer(
								base_type.member_class_entity(), false,
								base_type.pointer_levels().empty()
									? CVQualifier::None
									: base_type.pointer_levels().front().cv_qualifier));
				} else {
					for (const PointerLevel &pointer : base_type.pointer_levels()) {
						frames.front().prefixes.push_back(
							DeclaratorComponent::pointer(pointer.cv_qualifier));
					}
				}
				Token identifier;
				bool has_identifier = false;
				bool failed = false;
				bool has_member_pointer = base_type.has_member_class();
				bool member_pointer_is_function = false;
				// Location of the first pointer declarator, for the
				// pointer-to-reference rejection ([dcl.ref]/5).
				Token first_pointer_token;
				// Out-of-line declarator-id owner, for `int (*Owner::name)[N]`.
				StringHandle qualified_declarator_owner;
				auto attach_qualified_owner = [&](ASTNode& node) {
					if (qualified_declarator_owner.isValid() && node.is<DeclarationNode>()) {
						node.as<DeclarationNode>().set_qualified_declarator_owner(qualified_declarator_owner);
					}
				};
				CallingConvention member_function_calling_convention =
					CallingConvention::Default;
				std::optional<FunctionSignature> member_function_signature;
				std::optional<FlashCpp::ParsedParameterList> ordered_function_parameters;
				FlashCpp::MemberQualifiers ordered_function_qualifiers;
				FlashCpp::FunctionSpecifiers ordered_function_specifiers;
				CallingConvention ordered_function_calling_convention = CallingConvention::Default;

				while (!frames.empty() && !failed) {
					DeclaratorFrame &frame = frames.back();
					if (!frame.after_direct) {
						while (true) {
							if (peek() == "*"_tok) {
								if (first_pointer_token.value().empty()) {
									first_pointer_token = peek_info();
								}
								advance();
								CVQualifier pointer_cv = parse_cv_qualifiers();
								skip_noop_gnu_qualifiers();
								pointer_cv |= parse_cv_qualifiers();
								frame.prefixes.push_back(
									DeclaratorComponent::pointer(pointer_cv));
								continue;
							}
							if (peek() == "&&"_tok) {
								advance();
								skip_noop_gnu_qualifiers();
								frame.prefixes.push_back(
									DeclaratorComponent::rvalueReference());
								continue;
							}
							if (peek() == "&"_tok) {
								advance();
								skip_noop_gnu_qualifiers();
								frame.prefixes.push_back(
									DeclaratorComponent::lvalueReference());
								continue;
							}
							break;
						}
						if (peek().is_identifier()) {
							SaveHandle member_pointer_start = save_token_position();
							Token owner_token = peek_info();
							advance();
							const MemberPointerOwnerParse owner =
								parseMemberPointerOwnerAfterName(owner_token);
							if (consume("::"_tok) && consume("*"_tok)) {
								CVQualifier member_cv = parse_cv_qualifiers();
								skip_noop_gnu_qualifiers();
								member_cv |= parse_cv_qualifiers();
								applyMemberPointerOwner(base_type, owner);
								frame.prefixes.push_back(
									base_type.has_member_class_type_id()
										? DeclaratorComponent::memberPointer(
											base_type.member_class_type_id(), false,
											member_cv)
										: DeclaratorComponent::memberPointer(
											base_type.member_class_entity(), false,
											member_cv));
								member_function_calling_convention =
									frame.calling_convention;
								has_member_pointer = true;
								discard_saved_token(member_pointer_start);
								continue;
							} else {
								restore_token_position(member_pointer_start);
							}
						}
						if (peek() == "("_tok) {
							advance();
							const CallingConvention group_calling_convention =
								parse_calling_convention(CallingConvention::Default);
							// A calling convention written at the head of a parenthesized
							// declarator belongs to the function-pointer type, e.g.
							// `void (__vectorcall* p)(...)`, so it must reach the signature.
							if (group_calling_convention != CallingConvention::Default) {
								last_calling_convention_ = group_calling_convention;
							}
							frames.emplace_back();
							frames.back().delimited = true;
							frames.back().calling_convention =
								group_calling_convention;
							continue;
						}
						// An abstract parenthesized group may have an empty direct declarator before its
						// suffixes (`int (*[2])(int)`): after the ptr-operators the next token is a
						// suffix `[` or the closing `)` of the group.
						if ((peek() == ")"_tok || peek() == "["_tok) && !has_identifier && frame.delimited &&
								!frame.prefixes.empty()) {
							identifier = Token(Token::Type::Identifier, ""sv, current_token_.line(), current_token_.column(), current_token_.file_index());
							has_identifier = true;
							frame.after_direct = true;
							continue;
						}
						if (!peek().is_identifier() || has_identifier) {
							failed = true;
							break;
						}
						identifier = peek_info();
						has_identifier = true;
						advance();
						// A declarator-id may be qualified (`Owner::name`), as in an
						// out-of-line static data-member definition
						// `int (*Registry::table)[2][4]`. The member-pointer form
						// (`Owner::*`) was already handled above.
						while (peek() == "::"_tok) {
							if (!qualified_declarator_owner.isValid()) {
								qualified_declarator_owner = identifier.handle();
							}
							advance();
							if (!peek().is_identifier()) {
								failed = true;
								break;
							}
							identifier = peek_info();
							advance();
						}
						if (failed) {
							break;
						}
						frame.after_direct = true;
					}

					while (peek() == "["_tok) {
						const Token opening_bracket_token = peek_info();
						advance();
						if (peek() == "]"_tok) {
							frame.suffixes.push_back(
								DeclaratorComponent::unknownBoundArray());
							frame.suffix_array_bound_expressions.emplace_back();
							advance();
							continue;
						}
						ParseResult bound =
								parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
						if (bound.is_error() || !bound.node().has_value()) {
							failed = true;
							break;
						}
						const auto value = try_evaluate_constant_expression(*bound.node());
						const bool has_concrete_bound =
							value.has_value() && value->value > 0;
						// A dependent bound is retained as an unevaluated dimension
						// (payload 0) whenever a template context can substitute it
						// later, including a named declarator whose owner provides the
						// bound expressions rather than the alias/type-id out-param.
						if (!has_concrete_bound && !isDependentTemplateContext()) {
							failed = true;
							break;
						}
						frame.suffixes.push_back(has_concrete_bound
							? DeclaratorComponent::array(static_cast<size_t>(value->value))
							: DeclaratorComponent::array(0));
						frame.suffix_array_bound_expressions.push_back(*bound.node());
						if (!consume("]"_tok)) {
							// [dcl.array]/1: a bound must be closed by ']'. Report the
							// same diagnostic and opening-bracket note the flat path did.
							const SourceLocation opening_bracket_location = lexer_.getSourceLocation(opening_bracket_token);
							const SourceLocation unexpected_token_location = lexer_.getSourceLocation(peek_info());
							DiagnosticEngine& diagnostics_engine = context_.diagnostics();
							const uint32_t bracket_diagnostic_index = diagnostics_engine.reportWithRange(
								DiagnosticId::ExpectedCloseBracketAfterArraySize,
								DiagnosticSeverity::Error,
								unexpected_token_location,
								SourceRange::fromLocations(opening_bracket_location, unexpected_token_location),
								"Expected ']' after array size",
								{});
							diagnostics_engine.attachNote(
								bracket_diagnostic_index,
								DiagnosticId::NoteToMatchOpeningBracket,
								opening_bracket_location,
								"to match this '['",
								{});
							throw CompileError::fromStructuredDiagnostic(
								diagnostics_engine.diagnostic(bracket_diagnostic_index));
						}
					}
					if (failed) {
						break;
					}
					// A parameter-clause is a direct-declarator postfix. One per
					// declarator: the type has a single cold FunctionSignature.
					// Arrays of functions and functions returning arrays are
					// rejected after the spine is assembled.
					if (!has_member_pointer && peek() == "("_tok) {
						// A direct-declarator has at most one function suffix; a second
						// one in the same frame is `f()()` (a function returning a
						// function), which is ill-formed.
						if (!frame.suffixes.empty()) {
							failed = true;
							break;
						}
						// The parameter list can reset `last_calling_convention_`, so
						// capture the suffix's calling convention first; a convention
						// written inside the parenthesized group (`(__vectorcall* p)`)
						// belongs to this function-pointer type.
						const CallingConvention suffix_calling_convention = last_calling_convention_;
						FlashCpp::ParsedParameterList params;
						ParseResult param_result = parse_parameter_list(params);
						if (param_result.is_error()) {
							failed = true;
							break;
						}
						FlashCpp::MemberQualifiers qualifiers;
						FlashCpp::FunctionSpecifiers specifiers;
						ParseResult qualifier_result =
							parse_function_type_qualifiers(qualifiers, specifiers);
						if (qualifier_result.is_error()) {
							failed = true;
							break;
						}
						ordered_function_parameters = params;
						ordered_function_qualifiers = qualifiers;
						ordered_function_specifiers = specifiers;
						ordered_function_calling_convention = suffix_calling_convention;
						frame.suffix_functions.push_back(OrderedFunctionParams{std::move(params), qualifiers, specifiers, suffix_calling_convention});
						frame.suffixes.push_back(DeclaratorComponent::function());
					}
					if (frames.size() == 1 && has_member_pointer && peek() == "("_tok) {
						advance();
						std::vector<FunctionType> parameter_types;
						bool is_variadic = false;
						ParseResult parameter_result = parse_function_pointer_parameter_types(
							parameter_types, is_variadic);
						if (parameter_result.is_error() || !consume(")"_tok)) {
							failed = true;
							break;
						}
						FlashCpp::MemberQualifiers qualifiers;
						FlashCpp::FunctionSpecifiers specifiers;
						ParseResult qualifier_result =
							parse_function_type_qualifiers(qualifiers, specifiers);
						if (qualifier_result.is_error()) {
							failed = true;
							break;
						}
						FunctionSignature signature;
						// The member-pointer owner qualifies the pointer
						// component, not the callable's return type.  Do not
						// let it leak into the return type's identity.
						TypeSpecifierNode return_type_spec = base_type;
						return_type_spec.clear_member_class_identity();
						signature.setReturnType(makeFunctionType(return_type_spec));
						signature.return_type_index = return_type_spec.type_index();
						signature.setParameterTypes(std::move(parameter_types));
						signature.linkage = Linkage::None;
						signature.calling_convention =
							member_function_calling_convention;
						signature.is_variadic = is_variadic;
						apply_parsed_function_type_qualifiers(signature, qualifiers, specifiers);
						member_function_signature = std::move(signature);
						member_pointer_is_function = true;
					}

					if (frame.delimited) {
						if (peek() != ")"_tok) {
							failed = true;
							break;
						}
						advance();
					}

					std::vector<DeclaratorComponent> completed = std::move(frame.child);
					std::vector<ASTNode> completed_array_bound_expressions =
						std::move(frame.child_array_bound_expressions);
					std::vector<OrderedFunctionParams> completed_functions = std::move(frame.child_functions);
					completed.insert(completed.end(), frame.suffixes.begin(),
										 frame.suffixes.end());
					completed_array_bound_expressions.insert(
						completed_array_bound_expressions.end(),
						frame.suffix_array_bound_expressions.begin(),
						frame.suffix_array_bound_expressions.end());
					completed_functions.insert(completed_functions.end(),
						std::make_move_iterator(frame.suffix_functions.begin()), std::make_move_iterator(frame.suffix_functions.end()));
					for (auto pointer = frame.prefixes.rbegin();
							 pointer != frame.prefixes.rend(); ++pointer) {
						DeclaratorComponent component = *pointer;
						if (member_pointer_is_function &&
							component.kind == DeclaratorComponentKind::MemberObjectPointer) {
							component.kind = DeclaratorComponentKind::MemberFunctionPointer;
						}
						completed.push_back(component);
					}
					const bool was_delimited = frame.delimited;
					frames.pop_back();
					if (frames.empty()) {
						// The base type may already be an alias. [dcl.typedef]:
						// that alias is the type this declarator wraps, so its
						// array and reference stay inside the new spine.
						// [dcl.array] puts cv on the element; [dcl.ref] keeps a
						// reference return inside a function. Pointers from the
						// base are already the trailing prefixes.
						if (!completed.empty() && base_type.has_ordered_declarator()) {
							completed.insert(completed.end(), base_type.declarator_components().begin(), base_type.declarator_components().end());
						} else if (!completed.empty()) {
							std::vector<DeclaratorComponent> alias_inner;
							if (base_type.reference_qualifier() == ReferenceQualifier::LValueReference) {
								alias_inner.push_back(DeclaratorComponent::lvalueReference());
							} else if (base_type.reference_qualifier() == ReferenceQualifier::RValueReference) {
								alias_inner.push_back(DeclaratorComponent::rvalueReference());
							}
							const bool base_has_array = base_type.is_array() || base_type.has_pointee_array_declarator() ||
								base_type.has_unsized_outer_array_dimension();
							auto append_base_arrays = [&](std::vector<DeclaratorComponent>& dest) {
								if (base_type.array_dimensions().empty()) {
									dest.push_back(DeclaratorComponent::unknownBoundArray());
									return;
								}
								for (const size_t extent : base_type.array_dimensions()) {
									dest.push_back(extent == 0 ? DeclaratorComponent::unknownBoundArray() : DeclaratorComponent::array(extent));
								}
							};
							if (base_has_array && !base_type.has_pointee_array_declarator()) {
								append_base_arrays(alias_inner);
							}
							if (!alias_inner.empty()) {
								const size_t base_prefix_count = base_type.has_member_class() ? 1u : base_type.pointer_levels().size();
								const size_t insert_at = completed.size() >= base_prefix_count ? completed.size() - base_prefix_count : completed.size();
								completed.insert(completed.begin() + static_cast<std::ptrdiff_t>(insert_at), alias_inner.begin(), alias_inner.end());
							}
							if (base_has_array && base_type.has_pointee_array_declarator()) {
								append_base_arrays(completed);
							}
						}
						if (!has_identifier || peek() == "("_tok) {
							failed = true;
							break;
						}
						if (orderedReferenceComponentIsIllFormed(completed)) {
							failed = true;
							break;
						}
						// [dcl.ref]/5: no pointer or array of references. A reference
						// base type is only well-formed as a function return or a
						// reference declarator, so a pointer/array as the innermost
						// wrapper over it is ill-formed.
						if (base_type.is_reference() && !completed.empty()) {
							const DeclaratorComponentKind innermost = completed.back().kind;
							if (innermost == DeclaratorComponentKind::Pointer ||
								innermost == DeclaratorComponentKind::Array ||
								innermost == DeclaratorComponentKind::UnknownBoundArray) {
								const Token error_token = first_pointer_token.value().empty()
									? base_type.token()
									: first_pointer_token;
								const std::string message = "Cannot form a pointer to reference type";
								throw makeStructuredCompileError(
									context_.diagnostics(),
									DiagnosticId::PointerToReferenceType,
									DiagnosticSeverity::Error,
									lexer_.getSourceLocation(error_token),
									message,
									{});
							}
						}
						if (!completed_functions.empty()) {
							if (orderedFunctionComponentIsIllFormed(completed)) {
								failed = true;
								break;
							}
							if (completed_functions.size() > 1) {
								// A nested callable return such as `int (*(*p)())();` has one
								// Function component per callable. Build from the innermost
								// callable outward so each inner signature is available as the
								// return FunctionType's `callable_signature` when the enclosing
								// signature is formed. The explicit cursor keeps a
								// function-definition build (return type only) independent of
								// the full-type build.
								auto build_sub_spine = [&](auto&& self, size_t start, size_t cursor) -> TypeSpecifierNode {
									const size_t end = completed.size();
									TypeSpecifierNode node = base_type;
									node.clear_declarator_shape();
									if (start < end) {
										node.set_ordered_declarator(std::vector<DeclaratorComponent>(completed.begin() + static_cast<std::ptrdiff_t>(start), completed.end()));
									}
									std::optional<size_t> first_function;
									for (size_t index = start; index < end; ++index) {
										if (completed[index].kind == DeclaratorComponentKind::Function) {
											first_function = index;
											break;
										}
									}
									if (!first_function.has_value()) {
										return node;
									}
									TypeSpecifierNode return_type = self(self, *first_function + 1, cursor + 1);
									const OrderedFunctionParams& function = completed_functions[cursor];
									FunctionSignature signature;
									signature.setReturnType(makeFunctionType(return_type));
									std::vector<FunctionType> parameter_types;
									parameter_types.reserve(function.parameters.parameters.size());
									for (const ASTNode& parameter : function.parameters.parameters) {
										if (!parameter.is<DeclarationNode>()) {
											continue;
										}
										const DeclarationNode& parameter_decl = parameter.as<DeclarationNode>();
										FunctionType parameter_type = makeFunctionType(parameter_decl.type_specifier_node());
										if (parameter_decl.is_parameter_pack()) {
											parameter_type.is_pack_expansion = true;
										}
										parameter_types.push_back(std::move(parameter_type));
									}
									signature.setParameterTypes(std::move(parameter_types));
									signature.linkage = linkage;
									signature.calling_convention = function.calling_convention;
									signature.is_variadic = function.parameters.is_variadic;
									apply_parsed_function_type_qualifiers(signature, function.qualifiers, function.specifiers);
									node.set_ordered_declarator(std::vector<DeclaratorComponent>(completed.begin() + static_cast<std::ptrdiff_t>(start), completed.end()));
									node.set_function_signature(signature);
									return node;
								};
								std::optional<size_t> outermost_function;
								for (size_t index = 0; index < completed.size(); ++index) {
									if (completed[index].kind == DeclaratorComponentKind::Function) {
										outermost_function = index;
										break;
									}
								}
								const bool declares_nested_callable_function =
									outermost_function == 0 && !identifier.value().empty();
								if (declares_nested_callable_function) {
									// The entity is the outermost callable: a function definition
									// returning a (possibly nested) callable, such as
									// `int (*select(int which))(int) { ... }`.
									TypeSpecifierNode outer_type = build_sub_spine(build_sub_spine, 0, 0);
									TypeSpecifierNode return_type = build_sub_spine(build_sub_spine, 1, 1);
									ASTNode decl_node = emplace_node<DeclarationNode>(emplace_node<TypeSpecifierNode>(return_type), identifier);
									attach_qualified_owner(decl_node);
									auto func_decl_node = emplace_node<FunctionDeclarationNode>(decl_node.as<DeclarationNode>());
									FunctionDeclarationNode& func_ref = func_decl_node.as<FunctionDeclarationNode>();
									for (const ASTNode& parameter : completed_functions.front().parameters.parameters) {
										func_ref.add_parameter_node(parameter);
									}
									func_ref.set_is_variadic(completed_functions.front().parameters.is_variadic);
									func_ref.set_noexcept(outer_type.function_signature().is_noexcept);
									discard_saved_token(structural_start);
									return ParseResult::success(func_decl_node);
								}
								TypeSpecifierNode structural_type = build_sub_spine(build_sub_spine, 0, 0);
								if (identifier.value().empty()) {
									base_type = structural_type;
								}
								if (array_bound_expressions != nullptr) {
									array_bound_expressions->insert(array_bound_expressions->end(),
										completed_array_bound_expressions.begin(), completed_array_bound_expressions.end());
								}
								ASTNode decl_node;
								if (structural_type.is_array_object() || structural_type.has_pointee_array_declarator()) {
									decl_node = emplace_node<DeclarationNode>(
										emplace_node<TypeSpecifierNode>(structural_type), identifier,
										std::vector<ASTNode>(completed_array_bound_expressions));
								} else {
									decl_node = emplace_node<DeclarationNode>(emplace_node<TypeSpecifierNode>(structural_type), identifier);
								}
								attach_qualified_owner(decl_node);
								discard_saved_token(structural_start);
								return ParseResult::success(decl_node);
							}
							std::optional<size_t> function_index =
								singleFunctionComponentIndex(completed);
							if (!function_index.has_value()) {
								failed = true;
								break;
							}
							ReferenceQualifier function_pointer_reference =
								ReferenceQualifier::None;
							size_t function_pointer_component = 0;
							if (completed[0].kind == DeclaratorComponentKind::LValueReference ||
								completed[0].kind == DeclaratorComponentKind::RValueReference) {
								function_pointer_reference = completed[0].kind ==
										DeclaratorComponentKind::LValueReference
									? ReferenceQualifier::LValueReference
									: ReferenceQualifier::RValueReference;
								function_pointer_component = 1;
							}
							bool pointer_wrappers_before_function = true;
							for (size_t index = function_pointer_component;
								index < *function_index; ++index) {
								if (completed[index].kind !=
									DeclaratorComponentKind::Pointer) {
									pointer_wrappers_before_function = false;
									break;
								}
							}
							TypeSpecifierNode return_type = base_type;
							return_type.clear_declarator_shape();
							std::vector<DeclaratorComponent> return_components(
								completed.begin() +
									static_cast<std::ptrdiff_t>(*function_index + 1),
								completed.end());
							if (!return_components.empty()) {
								return_type.set_ordered_declarator(
									std::move(return_components));
								// The flat FunctionPointer representation carries
								// the return shape in the flat pointer/array
								// fields, not an ordered callable spine. When the
								// return projects, drop the ordered form so a
								// shape such as int* ((*p))() stays flat.
								if (return_type.ordered_declarator_has_legacy_projection()) {
									return_type.clear_ordered_declarator();
								}
							}
							// A bare function return with one or more pointer
							// wrappers is a (possibly nested) function-pointer
							// object. Keep it on the flat FunctionPointer
							// representation so ABI mangling does not need an
							// ordered callable spine yet (boundary 3B).
							const bool use_flat_function_pointer =
								!return_type.has_ordered_declarator() &&
								pointer_wrappers_before_function &&
								*function_index >= function_pointer_component + 1;
							if (return_type.has_ordered_declarator() &&
								return_type.ordered_declarator_has_legacy_projection() &&
								!use_flat_function_pointer) {
								failed = true;
								break;
							}
							// The base type is the callable's return type; a reference return
							// (`Big& (*fp)()`) is not a spine component, so restore it after the
							// declarator shape was cleared.
							return_type.set_reference_qualifier(base_type.reference_qualifier());
							FunctionSignature signature;
							signature.setReturnType(
								makeFunctionType(return_type));
							std::vector<FunctionType> parameter_types;
							parameter_types.reserve(
								ordered_function_parameters->parameters.size());
							bool parameters_ok = true;
							for (const ASTNode& parameter :
									ordered_function_parameters->parameters) {
								if (!parameter.is<DeclarationNode>()) {
									parameters_ok = false;
									break;
								}
								const DeclarationNode& parameter_decl = parameter.as<DeclarationNode>();
								FunctionType parameter_type = makeFunctionType(parameter_decl.type_specifier_node());
								// parse_parameter_list records a pack on the declaration; mirror it
								// onto the structured component so signature substitution expands it.
								if (parameter_decl.is_parameter_pack()) {
									parameter_type.is_pack_expansion = true;
								}
								parameter_types.push_back(std::move(parameter_type));
							}
							if (!parameters_ok) {
								failed = true;
								break;
							}
							signature.setParameterTypes(std::move(parameter_types));
							signature.linkage = linkage;
							signature.calling_convention = ordered_function_calling_convention;
							signature.is_variadic =
								ordered_function_parameters->is_variadic;
							apply_parsed_function_type_qualifiers(
								signature,
								ordered_function_qualifiers,
								ordered_function_specifiers);
							const bool declares_function =
								*function_index == 0 && !identifier.value().empty();
							if (declares_function) {
								auto decl_node = emplace_node<DeclarationNode>(
									emplace_node<TypeSpecifierNode>(return_type),
									identifier);
								attach_qualified_owner(decl_node);
								auto func_decl_node = emplace_node<FunctionDeclarationNode>(
									decl_node.as<DeclarationNode>());
								FunctionDeclarationNode& func_ref =
									func_decl_node.as<FunctionDeclarationNode>();
								for (const ASTNode& parameter :
										ordered_function_parameters->parameters) {
									func_ref.add_parameter_node(parameter);
								}
								func_ref.set_is_variadic(
									ordered_function_parameters->is_variadic);
								func_ref.set_noexcept(signature.is_noexcept);
								discard_saved_token(structural_start);
								return ParseResult::success(func_decl_node);
							}
							if (use_flat_function_pointer) {
								// Flat FunctionPointer encoding:
								//   int (*)(Args)  → category only (empty levels)
								//   int (**)(Args) → category + full pointer_levels
								// Import adds one wrap for empty levels, or applies
								// pointer_levels as the complete wrapper stack when
								// non-empty (so nested depth stays distinct without
								// double-counting a category wrap).
								const size_t first_pointer_index = function_pointer_component;
								const size_t last_pointer_index = *function_index - 1;
								const size_t pointer_wrapper_count =
									last_pointer_index - first_pointer_index + 1;
								const CVQualifier function_pointer_cv =
									pointer_wrapper_count == 1
										? completed[last_pointer_index].cv_qualifier
										: CVQualifier::None;
								ASTNode function_pointer_node = emplace_node<TypeSpecifierNode>(
									TypeCategory::FunctionPointer,
									TypeQualifier::None,
									SizeInBits{kFunctionPointerSizeBits},
									base_type.token(),
									function_pointer_cv);
								TypeSpecifierNode& function_pointer =
									function_pointer_node.as<TypeSpecifierNode>();
								function_pointer.set_function_signature(signature);
								function_pointer.set_reference_qualifier(
									function_pointer_reference);
								if (pointer_wrapper_count > 1) {
									for (size_t index = first_pointer_index;
										index <= last_pointer_index; ++index) {
										function_pointer.add_pointer_level(
											completed[index].cv_qualifier);
									}
								}
								if (identifier.value().empty()) {
									base_type = function_pointer;
								}
								ASTNode function_pointer_decl = emplace_node<DeclarationNode>(
									std::move(function_pointer_node),
									identifier);
								attach_qualified_owner(function_pointer_decl);
								discard_saved_token(structural_start);
								return ParseResult::success(function_pointer_decl);
							}
							TypeSpecifierNode structural_type = base_type;
							structural_type.clear_declarator_shape();
							structural_type.set_ordered_declarator(std::move(completed));
							structural_type.set_function_signature(signature);
							if (identifier.value().empty()) {
								base_type = structural_type;
							}
							if (array_bound_expressions != nullptr) {
								array_bound_expressions->insert(
									array_bound_expressions->end(),
									completed_array_bound_expressions.begin(),
									completed_array_bound_expressions.end());
							}
							// An array object owns inline storage; attach the parsed bound expressions so the
							// declaration reports owns_inline_array_storage() for layout and initialization.
							ASTNode callable_decl_node;
							if (structural_type.is_array_object() || structural_type.has_pointee_array_declarator()) {
								callable_decl_node = emplace_node<DeclarationNode>(
									emplace_node<TypeSpecifierNode>(structural_type), identifier,
									std::vector<ASTNode>(completed_array_bound_expressions));
							} else {
								callable_decl_node = emplace_node<DeclarationNode>(emplace_node<TypeSpecifierNode>(structural_type), identifier);
							}
							attach_qualified_owner(callable_decl_node);
							discard_saved_token(structural_start);
							return ParseResult::success(callable_decl_node);
						}
						// Redundant parentheses around a declarator-id, as in
						// `int& (r) = x`, add no component. Keep the decl-specifier
						// shape; an empty ordered spine would clear its reference.
						if (completed.empty() && !has_member_pointer) {
							ASTNode plain_decl = emplace_node<DeclarationNode>(emplace_node<TypeSpecifierNode>(base_type), identifier);
							attach_qualified_owner(plain_decl);
							discard_saved_token(structural_start);
							return ParseResult::success(plain_decl);
						}
						TypeSpecifierNode structural_type = base_type;
						if (member_pointer_is_function) {
							for (DeclaratorComponent& component : completed) {
								if (component.kind == DeclaratorComponentKind::MemberObjectPointer) {
									component.kind = DeclaratorComponentKind::MemberFunctionPointer;
								}
							}
						}
						bool bare_member_pointer = false;
						if (has_member_pointer) {
							bare_member_pointer = true;
							for (const DeclaratorComponent& component : completed) {
								if (component.kind != DeclaratorComponentKind::MemberObjectPointer &&
									component.kind != DeclaratorComponentKind::MemberFunctionPointer) {
									bare_member_pointer = false;
									break;
								}
							}
						}
						if (bare_member_pointer) {
							// A pointer-to-member that is not interleaved with
							// pointer/array declarators keeps the legacy flat
							// projection, which still carries its owner and
							// callable payload to unmigrated consumers. Only
							// interleaved member pointers need the ordered spine.
							for (const DeclaratorComponent& component : completed) {
								if (component.kind == DeclaratorComponentKind::MemberFunctionPointer) {
									structural_type.set_type_index(
										nativeTypeIndex(TypeCategory::MemberFunctionPointer));
									structural_type.set_size_in_bits(kFunctionPointerSizeBits);
									if (member_function_signature.has_value()) {
										structural_type.set_function_signature(
											*member_function_signature);
									}
								} else {
									structural_type.add_pointer_level(component.cv_qualifier);
								}
							}
						} else {
							structural_type.set_ordered_declarator(std::move(completed));
							if (member_function_signature.has_value()) {
								structural_type.set_type_index(
									nativeTypeIndex(TypeCategory::MemberFunctionPointer));
								structural_type.set_size_in_bits(kFunctionPointerSizeBits);
								structural_type.set_function_signature(*member_function_signature);
							}
						}
						if (identifier.value().empty()) {
							base_type = structural_type;
						}
						if (array_bound_expressions != nullptr) {
							array_bound_expressions->insert(
								array_bound_expressions->end(),
								completed_array_bound_expressions.begin(),
								completed_array_bound_expressions.end());
						}
						// An array object owns inline storage; attach the parsed bound expressions so the
						// declaration reports owns_inline_array_storage() for layout and initialization.
						ASTNode ordered_decl_node;
						if (structural_type.is_array_object() || structural_type.has_pointee_array_declarator()) {
							ordered_decl_node = emplace_node<DeclarationNode>(
								emplace_node<TypeSpecifierNode>(structural_type), identifier,
								std::vector<ASTNode>(completed_array_bound_expressions));
						} else {
							ordered_decl_node = emplace_node<DeclarationNode>(emplace_node<TypeSpecifierNode>(structural_type), identifier);
						}
						attach_qualified_owner(ordered_decl_node);
						discard_saved_token(structural_start);
						return ParseResult::success(ordered_decl_node);
					}
					if (!was_delimited || frames.back().after_direct) {
						failed = true;
						break;
					}
					frames.back().child = std::move(completed);
					frames.back().child_array_bound_expressions =
						std::move(completed_array_bound_expressions);
					frames.back().child_functions = std::move(completed_functions);
					frames.back().after_direct = true;
				}
				restore_token_position(structural_start);
				discard_saved_token(structural_start);
			}
		}
		return std::nullopt;
	};
	if (std::optional<ParseResult> structural =
					tryStructuralPointerArrayDeclarator()) {
		return std::move(*structural);
	}
	// [dcl.decl] noptr-declarator: ( ptr-declarator ). The frame-stack grammar
	// owns that group. Do not parse the parenthesis as a flat function suffix.
	if (peek() == "("_tok) {
		return ParseResult::error("Expected declarator", current_token_);
	}

	// Handle pointer prefix: * [const] [volatile] *...
	while (peek() == "*"_tok) {
		advance(); // consume '*'

		// Parse CV-qualifiers after the *
		CVQualifier ptr_cv = parse_cv_qualifiers();
		skip_noop_gnu_qualifiers();
		ptr_cv |= parse_cv_qualifiers();

		base_type.add_pointer_level(ptr_cv);
	}

	skip_noop_gnu_qualifiers();

	// Parse direct declarator (identifier, function, array)
	Token identifier_token;
	return parse_direct_declarator(base_type, identifier_token, linkage);
}

// NEW: Parse direct declarator (identifier, function, array)
ParseResult Parser::parse_direct_declarator(TypeSpecifierNode& base_type,
											Token& out_identifier,
											Linkage linkage) {
	// For now, we'll handle the simple case: identifier followed by optional function params
	// TODO: Handle parenthesized declarators like (*fp)(params) for function pointers

	// Parse identifier
	if (!peek().is_identifier()) {
		return ParseResult::error("Expected identifier in declarator",
								  current_token_);
	}

	out_identifier = peek_info();
	advance();

	// Parse postfix operators (function, array)
	return parse_postfix_declarator(
		base_type, out_identifier, linkage, last_calling_convention_);
}

// NEW: Parse postfix declarators (function, array)
ParseResult Parser::parse_postfix_declarator(TypeSpecifierNode& base_type,
											 const Token& identifier,
											 Linkage linkage,
											 CallingConvention calling_convention) {
	std::optional<std::string_view> asm_symbol_name;
	// Check for function declarator: '(' params ')'
	if (peek() == "("_tok) {
		advance(); // consume '('

		// Parse parameter list
		std::vector<FunctionType> param_types;
		bool is_variadic = false;
		auto param_result = parse_function_pointer_parameter_types(param_types, is_variadic);
		if (param_result.is_error()) {
			return param_result;
		}

		if (!consume(")"_tok)) {
			return ParseResult::error("Expected ')' after function parameters",
									  current_token_);
		}

		FlashCpp::MemberQualifiers qualifiers;
		FlashCpp::FunctionSpecifiers specifiers;
		ParseResult qualifier_result =
			parse_function_type_qualifiers(qualifiers, specifiers);
		if (qualifier_result.is_error()) {
			return qualifier_result;
		}
		skip_asm_suffix(&asm_symbol_name);

		// This is a function pointer!
		// The base_type is the return type
		// We need to create a function pointer type

		TypeIndex return_type_index = base_type.type_index();

		// Create a new TypeSpecifierNode for the function pointer
		// Function pointers are 64 bits (8 bytes) on x64
		TypeSpecifierNode fp_type(TypeCategory::FunctionPointer, TypeQualifier::None, kFunctionPointerSizeBits, Token{}, CVQualifier::None);

		FunctionSignature sig;
		sig.setReturnType(makeFunctionType(base_type));
		sig.return_type_index = return_type_index;
		sig.return_pointer_depth = static_cast<int>(base_type.pointer_depth());
		sig.return_reference_qualifier = base_type.reference_qualifier();
		sig.setParameterTypes(std::move(param_types));
		sig.linkage = linkage;
		sig.calling_convention = calling_convention;
		sig.is_variadic = is_variadic;
		apply_parsed_function_type_qualifiers(sig, qualifiers, specifiers);
		fp_type.set_function_signature(sig);

		// Replace base_type with the function pointer type
		base_type = fp_type;
	}

	std::vector<ASTNode> array_dimensions;
	bool is_unsized_array = false;
	while (peek() == "["_tok) {
		advance(); // consume '['

		if (peek() == "]"_tok) {
			if (array_dimensions.empty()) {
				is_unsized_array = true;
			} else {
				return ParseResult::error("Only the first array dimension can be unsized", current_token_);
			}
		} else {
			auto size_result = parse_expression(DEFAULT_PRECEDENCE, ExpressionContext::Normal);
			if (size_result.is_error()) {
				return size_result;
			}
			array_dimensions.push_back(*size_result.node());
		}

		if (peek().is_eof() || peek_info().type() != Token::Type::Punctuator || peek() != "]"_tok) {
			return ParseResult::error("Expected ']' after array size", current_token_);
		}
		advance(); // consume ']'
	}

	if (!array_dimensions.empty() || is_unsized_array) {
		base_type.set_array(true);
	}

	// Consume any __asm__("...") suffix that appears after array dimensions
	// on non-function declarators (the function-pointer branch already captured
	// it at line 1067).
	skip_asm_suffix(&asm_symbol_name);

	// Create and return declaration node
	ASTNode decl_node;
	if (!array_dimensions.empty()) {
		decl_node = emplace_node<DeclarationNode>(
			emplace_node<TypeSpecifierNode>(base_type),
			identifier,
			std::move(array_dimensions));
	} else {
		decl_node = emplace_node<DeclarationNode>(
			emplace_node<TypeSpecifierNode>(base_type),
			identifier);
	}
	if (asm_symbol_name.has_value()) {
		decl_node.as<DeclarationNode>().set_mangled_name(*asm_symbol_name);
	}
	return ParseResult::success(decl_node);
}

// Parse the parameter-type-list that appears inside a function pointer declarator.
// This shared helper covers both standalone function pointers and function
// declarations whose return type is itself a function pointer.
ParseResult Parser::parse_function_pointer_parameter_types(std::vector<FunctionType>& out_param_types, bool& out_is_variadic) {
	out_is_variadic = false;
	if (peek() == ")"_tok) {
		return ParseResult::success();
	}

	while (true) {
		// Accept standalone or terminal variadic ellipses, including MSVC header aliases like bool(__stdcall*)(...) noexcept.
		if (peek() == "..."_tok) {
			advance();
			out_is_variadic = true;
			break;
		}

		auto param_type_result = parse_type_specifier();
		if (param_type_result.is_error()) {
			return param_type_result;
		}

		TypeSpecifierNode& param_type = param_type_result.node()->as<TypeSpecifierNode>();
		ParseResult nested_function_result =
			parse_nested_function_pointer_type(param_type);
		if (nested_function_result.is_error()) {
			return nested_function_result;
		}
		skip_noop_gnu_qualifiers();
		consume_pointer_ref_modifiers(param_type);
		if (peek() == "..."_tok) {
			advance();
			param_type.set_pack_expansion(true);
			if (peek() == "..."_tok) {
				advance();
			}
		}

		if (peek().is_identifier()) {
			advance();
		}
		out_param_types.push_back(makeFunctionType(param_type));

		if (peek() == ","_tok) {
			advance();
		} else {
			break;
		}
	}

	return ParseResult::success();
}

ParseResult Parser::parse_nested_function_pointer_type(
	TypeSpecifierNode& type_spec) {
	if (peek() != "("_tok) {
		return ParseResult::success();
	}
	// [dcl.decl]: a nested declarator is a parenthesized ptr-declarator, not
	// only the `(*` spelling. parse_declarator owns the group.
	SaveHandle nested_start = save_token_position();
	ParseResult declarator_result = parse_declarator(type_spec, Linkage::None);
	std::optional<ASTNode> declarator_node = declarator_result.node();
	const DeclarationNode* declaration = declarator_node ? declarator_node->get_if<DeclarationNode>() : nullptr;
	if (declarator_result.is_error() || declaration == nullptr) {
		restore_token_position(nested_start);
		return declarator_result.is_error() ? std::move(declarator_result) : ParseResult::error("Expected declarator", current_token_);
	}
	type_spec = declaration->type_specifier_node();
	discard_saved_token(nested_start);
	return ParseResult::success();
}

// Phase 1 Consolidation: Parse declaration specifiers shared between
// parse_declaration_or_function_definition() and parse_variable_declaration()
// Handles: attributes ([[nodiscard]], __declspec), storage class (static, extern),
// CV-qualifiers, constexpr/constinit/consteval, inline specifiers
FlashCpp::DeclarationSpecifiers Parser::parse_declaration_specifiers() {
	FlashCpp::DeclarationSpecifiers specs;
	auto merge_attributes = [&](const AttributeInfo& attributes) {
		if (attributes.linkage != Linkage::None) {
			specs.linkage = attributes.linkage;
		}
		if (attributes.calling_convention != CallingConvention::Default) {
			specs.calling_convention = attributes.calling_convention;
		}
		specs.is_selectany |= attributes.is_selectany;
		specs.is_noreturn |= attributes.is_noreturn;
	};
	auto tryParseMsvcDeclspec = [&]() {
		AttributeInfo attributes;
		if (!tryParseDeclspecSpecifiers(attributes)) {
			return false;
		}
		merge_attributes(attributes);
		return true;
	};

	// Parse any attributes before the declaration ([[nodiscard]], __declspec(dllimport), __cdecl, etc.)
	merge_attributes(parse_attributes());

	// Parse storage class specifiers and constexpr/constinit/consteval keywords
	// These can appear in any order: "static constexpr", "constexpr static", etc.
	while (true) {
		if (tryParseMsvcDeclspec()) {
			continue;
		}

		const TokenKind keyword = peek();
		if (!keyword.is_keyword()) {
			break;
		}

		if (keyword == "constexpr"_tok) {
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Constexpr;
			advance();
		} else if (keyword == "constinit"_tok) {
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Constinit;
			advance();
		} else if (keyword == "consteval"_tok) {
			specs.constexpr_spec = FlashCpp::ConstexprSpecifier::Consteval;
			advance();
		} else if (keyword == "inline"_tok ||
				   keyword == "__inline"_tok ||
				   keyword == "__forceinline"_tok) {
			specs.is_inline = true;
			advance();
		} else if (keyword == "static"_tok) {
			specs.storage_class = StorageClass::Static;
			advance();
		} else if (keyword == "extern"_tok) {
			specs.storage_class = StorageClass::Extern;
			advance();
		} else if (keyword == "thread_local"_tok ||
				   keyword == "__thread"_tok) {
			specs.is_thread_local = true;
			advance();
		} else if (keyword == "register"_tok) {
			specs.storage_class = StorageClass::Register;
			advance();
		} else if (keyword == "mutable"_tok) {
			specs.storage_class = StorageClass::Mutable;
			advance();
		} else if (keyword == "const"_tok) {
			specs.cv_qualifier |= CVQualifier::Const;
			advance();
		} else if (keyword == "volatile"_tok) {
			specs.cv_qualifier |= CVQualifier::Volatile;
			advance();
		} else {
			break;
		}
	}

	// Also skip any GCC attributes that might appear after storage class specifiers
	skip_gcc_attributes();

	// Apply last calling convention if none was explicitly specified
	if (specs.calling_convention == CallingConvention::Default &&
		last_calling_convention_ != CallingConvention::Default) {
		specs.calling_convention = last_calling_convention_;
	}

	return specs;
}

// Phase 2 Consolidation: Lookahead to detect if '(' starts function parameters
// vs direct initialization (e.g., `int x(10)` vs `int func(int y)`)
//
// Returns true if current position is at '(' and it looks like function parameters:
// - `int x()` - empty = function (prefer function over value-init variable)
// - `int x(int y)` - starts with type = function params
// - `int x(10)` - starts with literal/expression = direct init (return false)
// - `int x(a)` where `a` is a type = function params (return true)
// - `int x(a)` where `a` is a variable = direct init (return false)
//
// This uses lookahead without consuming tokens.
bool Parser::bareQualifiedTerminalIsValue(const TypeInfo* owner) {
	SaveHandle qualified_saved = save_token_position();
	auto restore_before_return = [&](bool result) {
		restore_token_position(qualified_saved);
		return result;
	};
	if (owner != nullptr && peek() == "<"_tok) {
		const std::string_view template_name = StringTable::getStringView(owner->name());
		const auto template_declaration = gTemplateRegistry.lookupTemplate(template_name);
		const bool is_class_template = template_declaration.has_value() && template_declaration->is<TemplateClassDeclarationNode>();
		if (!is_class_template && !gTemplateRegistry.lookup_alias_template(template_name).has_value()) {
			return restore_before_return(false);
		}

		ScopedParserInstantiationContext shape_probe(*this, TemplateInstantiationMode::ShapeOnly, StringHandle{});
		auto template_args = parse_explicit_template_arguments();
		if (!template_args.has_value() || peek() != "::"_tok) {
			return restore_before_return(false);
		}

		std::optional<ASTNode> instantiated = try_instantiate_class_template(template_name, *template_args, false);
		const TypeInfo* instantiated_owner = nullptr;
		if (instantiated.has_value() && instantiated->is<StructDeclarationNode>()) {
			const StructDeclarationNode& instantiated_struct = instantiated->as<StructDeclarationNode>();
			instantiated_owner = findTypeByName(instantiated_struct.semantic_name());
			if (instantiated_owner == nullptr) {
				instantiated_owner = findTypeByName(instantiated_struct.name());
			}
		}
		if (instantiated_owner == nullptr) {
			const StringHandle instantiated_name = StringTable::getOrInternStringHandle(get_instantiated_class_name(template_name, *template_args));
			instantiated_owner = findTypeByName(instantiated_name);
		}
		if (instantiated_owner == nullptr) {
			return restore_before_return(false);
		}
		owner = instantiated_owner;
	}
	if (owner != nullptr && owner->getEnumInfo() != nullptr && peek() == "::"_tok) {
		return restore_before_return(true);
	}
	const TypeInfo* current = owner;
	while (peek() == "::"_tok) {
		advance();
		if (!peek().is_identifier()) {
			return restore_before_return(false);
		}
		const StringHandle member_name = StringTable::getOrInternStringHandle(peek_info().value());
		advance();
		// A call or member template-id stays with the qualified-call lookahead.
		if (peek() == "("_tok || peek() == "<"_tok) {
			return restore_before_return(false);
		}
		const QualifiedMemberClassification classified = classifyQualifiedMember(current, member_name);
		if (peek() == "::"_tok) {
			if (classified.kind != QualifiedMemberKind::Type || classified.type_info == nullptr) {
				return restore_before_return(false);
			}
			current = classified.type_info;
			continue;
		}
		const bool non_type = classified.kind == QualifiedMemberKind::Value || classified.kind == QualifiedMemberKind::Ambiguous;
		return restore_before_return(non_type);
	}
	return restore_before_return(false);
}

bool Parser::qualifiedTypeNameDenotesNonType(std::string_view type_name) const {
	const size_t first_scope = type_name.find("::");
	if (first_scope == std::string_view::npos) {
		return false;
	}
	const StringHandle owner_name = StringTable::getOrInternStringHandle(type_name.substr(0, first_scope));
	const auto owner_iter = getTypesByNameMap().find(owner_name);
	if (owner_iter == getTypesByNameMap().end() || owner_iter->second == nullptr) {
		return false;
	}
	const TypeInfo* owner = owner_iter->second;
	size_t member_start = first_scope + 2;
	while (member_start < type_name.size()) {
		const size_t next_scope = type_name.find("::", member_start);
		const size_t member_length = next_scope == std::string_view::npos ? std::string_view::npos : next_scope - member_start;
		const std::string_view member_spelling = type_name.substr(member_start, member_length);
		const QualifiedMemberClassification member = classifyQualifiedMember(owner, StringTable::getOrInternStringHandle(member_spelling));
		if (member.kind == QualifiedMemberKind::Value || member.kind == QualifiedMemberKind::Ambiguous) {
			return true;
		}
		if (member.kind != QualifiedMemberKind::Type || member.type_info == nullptr || next_scope == std::string_view::npos) {
			return false;
		}
		owner = member.type_info;
		member_start = next_scope + 2;
	}
	return false;
}

bool Parser::looks_like_function_parameters() {
	// Must be at '('
	if (peek() != "("_tok) {
		return false;
	}

	// Save current position for restoration
	SaveHandle saved = save_token_position();

	advance();  // consume '('

	// Empty parens: `int x()` - prefer function
	if (peek() == ")"_tok) {
		restore_token_position(saved);
		return true;
	}

	// Look at what's after '('
	// If it starts with a type keyword, it's likely function parameters
	// If it starts with a literal or identifier that's not a type, it's likely direct init

	if (!peek().is_eof()) {
		Token::Type token_type = peek_info().type();
		std::string_view token_value = peek_info().value();
			auto looks_like_qualified_direct_init_arg = [&]() -> bool {
				SaveHandle qualified_saved = save_token_position();
				auto restore_before_return = [&](bool result) {
					restore_token_position(qualified_saved);
					return result;
				};
				if (peek() == "<"_tok) {
					skip_template_arguments();
				}
				if (peek() != "::"_tok) {
					return restore_before_return(false);
				}
				while (peek() == "::"_tok) {
					advance(); // consume '::'
					if (!peek().is_identifier()) {
						return restore_before_return(false);
					}
					advance(); // consume the qualified-name component
					if (peek() == "<"_tok) {
						skip_template_arguments();
					}
				}
				return restore_before_return(peek() == "("_tok);
			};

		// Literals (numbers, strings) = direct initialization
		if (token_type == Token::Type::Literal) {
			restore_token_position(saved);
			return false;
		}

		// Type keywords = function parameters
		static const std::unordered_set<std::string_view> param_type_keywords = {
			"int", "float", "double", "char", "bool", "void",
			"short", "long", "signed", "unsigned", "const", "volatile",
			"auto", "decltype", "struct", "class", "enum", "union",
			"wchar_t", "char8_t", "char16_t", "char32_t",
			"__int8", "__int16", "__int32", "__int64"};

		if (token_type == Token::Type::Keyword && param_type_keywords.count(token_value)) {
			restore_token_position(saved);
			return true;
		}

		// For identifiers, we need to check if it's a known type
		if (token_type == Token::Type::Identifier) {
			StringHandle id_handle = StringTable::getOrInternStringHandle(token_value);

			// A current non-type template parameter (e.g. N in `Box box(N);`) is an
			// expression argument, not an unnamed parameter type, so prefer direct-init.
			bool is_current_template_param = false;
			for (StringHandle param_name : currentTemplateParamNames()) {
				if (param_name == id_handle) {
					is_current_template_param = true;
					break;
				}
			}
			if (is_current_template_param) {
				bool is_type_template_param = false;
				for (const auto& subst : template_param_substitutions_) {
					if (subst.param_name == id_handle) {
						is_type_template_param = subst.is_type_param;
						break;
					}
				}
				if (!is_type_template_param) {
					restore_token_position(saved);
					return false;
				}
			}

			// Check if this identifier is a known type in the type registry
			auto type_iter = getTypesByNameMap().find(id_handle);
			if (type_iter != getTypesByNameMap().end()) {
				// Before deciding it's a function parameter type, check if '::' follows.
				// Qualified-id expressions such as `Factory::make(7)`,
				// `Factory<int>::make(7)`, or `Color::Green` are
				// direct-initialization arguments, not parameter declarations.
				advance();  // consume the identifier
				if (peek() == "{"_tok) {
					restore_token_position(saved);
					return false;
				}
				if (type_iter->second != nullptr && (peek() == "::"_tok || peek() == "<"_tok) && bareQualifiedTerminalIsValue(type_iter->second)) {
					restore_token_position(saved);
					return false;
				}

				if (looks_like_qualified_direct_init_arg()) {
					// `KnownType::name(...)` is a qualified call expression used as a
					// direct-initializer argument, not a function parameter list.
					restore_token_position(saved);
					return false;
				}
				// It's a known type = function parameters
				restore_token_position(saved);
				return true;
			}

			// Check if it's in current scope as a variable
			auto symbol_lookup = gSymbolTable.lookup(token_value);
			if (symbol_lookup.has_value()) {
				// It's a variable = direct initialization
				restore_token_position(saved);
				return false;
			}

			// Unknown identifier - check if next token gives us more context
			// e.g., `int x(MyType y)` where 'y' is identifier = function param
			// e.g., `int x(a + b)` where '+' follows = expression = direct init
			advance();  // consume the identifier
			if (looks_like_qualified_direct_init_arg()) {
				restore_token_position(saved);
				return false;
			}

			if (!peek().is_eof()) {
				std::string_view next_val = peek_info().value();
				// If followed by an identifier, it's likely `type name` = function param
				if (peek().is_identifier()) {
					restore_token_position(saved);
					return true;
				}
				// If followed by `)` or `,` it could be either - check if identifier is uppercase (likely type)
				if (next_val == ")" || next_val == ",") {
					// Heuristic: if the identifier starts with uppercase, assume it's a type
					// This handles common cases like `int x(MyClass)`
					if (!token_value.empty() && std::isupper(static_cast<unsigned char>(token_value[0]))) {
						restore_token_position(saved);
						return true;
					}
				}
				// If followed by operators (+, -, *, etc.) = expression = direct init
				if (peek_info().type() == Token::Type::Operator) {
					restore_token_position(saved);
					return false;
				}
				// If followed by pointer/reference declarators (* or &) = function param with pointer type
				if (next_val == "*" || next_val == "&") {
					restore_token_position(saved);
					return true;
				}
			}

			restore_token_position(saved);
			// Default to false for unknown identifiers (direct initialization)
			return false;
		}

		// Pointer/reference operators at start
		// Could be function with complex return type, OR dereference expression like *this
		if (token_value == "*" || token_value == "&") {
			// Peek ahead to see what follows the operator
			advance();  // consume the '*' or '&'

			if (!peek().is_eof()) {
				std::string_view after_op = peek_info().value();
				Token::Type after_op_type = peek_info().type();

				// If followed by 'this', it's *this or &this = direct initialization
				if (after_op == "this") {
					restore_token_position(saved);
					return false;
				}

				// If followed by a variable (identifier in symbol table), it's *var or &var = expression
				if (after_op_type == Token::Type::Identifier) {
					auto symbol_lookup = gSymbolTable.lookup(after_op);
					if (symbol_lookup.has_value()) {
						restore_token_position(saved);
						return false;
					}
				}

				// If followed by a literal, it's an expression (though unusual)
				if (after_op_type == Token::Type::Literal) {
					restore_token_position(saved);
					return false;
				}

				// If followed by an open paren, it could be (*expr) = expression
				if (after_op == "(") {
					restore_token_position(saved);
					return false;
				}
			}

			restore_token_position(saved);
			// Otherwise, assume function with pointer parameter
			return true;
		}
	}

	restore_token_position(saved);
	// Default: unknown, assume not function params
	return false;
}

// Phase 4: Unified declaration parsing
// This is the single entry point for parsing all declarations (variables and functions)
// It delegates to the appropriate specialized parsing function based on context
ParseResult Parser::parse_declaration(FlashCpp::DeclarationContext context) {
	FLASH_LOG_FORMAT(Parser, Debug, "parse_declaration: Starting, context={}, current token: {}",
					 static_cast<int>(context),
					 !peek().is_eof() ? std::string(peek_info().value()) : "N/A");

	// Determine actual context if Auto
	FlashCpp::DeclarationContext effective_context = context;
	if (context == FlashCpp::DeclarationContext::Auto) {
		// Infer from current scope type
		ScopeType current_scope = gSymbolTable.get_current_scope_type();
		switch (current_scope) {
		case ScopeType::Global:
		case ScopeType::Namespace:
			effective_context = FlashCpp::DeclarationContext::TopLevel;
			break;
		case ScopeType::Function:
		case ScopeType::Block:
			effective_context = FlashCpp::DeclarationContext::BlockScope;
			break;
		default:
			effective_context = FlashCpp::DeclarationContext::BlockScope;
			break;
		}
	}

	// Delegate to appropriate specialized parser based on context
	switch (effective_context) {
	case FlashCpp::DeclarationContext::TopLevel:
			// Top-level uses parse_declaration_or_function_definition
			// It handles: function definitions, global variables, out-of-line member functions
		return parse_declaration_or_function_definition();

	case FlashCpp::DeclarationContext::BlockScope:
	case FlashCpp::DeclarationContext::ForInit:
	case FlashCpp::DeclarationContext::IfInit:
	case FlashCpp::DeclarationContext::SwitchInit:
			// Block scope uses parse_variable_declaration
			// It handles: local variables, direct init, structured bindings, and now also
			// function declarations (delegated via looks_like_function_parameters)
		return parse_variable_declaration();

	case FlashCpp::DeclarationContext::ClassMember:
			// Class member declarations are handled by parse_struct_declaration
			// This shouldn't be called directly for class members
		return ParseResult::error("Class member declarations should use parse_struct_declaration", current_token_);

	default:
		return ParseResult::error("Unknown declaration context", current_token_);
	}
}
FunctionType Parser::makeFunctionType(const TypeSpecifierNode& type_spec) const {
	TypeSpecifierNode bound_type = type_spec;
	tryBindPublishedTypeEntity(bound_type);
	FunctionType type = makeFunctionTypeFromSpecifier(bound_type);
	// The compatibility component cannot retain an alias's argument graph.
	// Preserve its declaration and parameter identities before that projection.
	if (bound_type.is_alias_template_specialization()) {
		CanonicalTypeTable& table = requireFrontendContext().canonicalTypes();
		CanonicalTypeTransaction transaction(table);
		const CanonicalTypeImport imported = importCanonicalType(table, bound_type);
		if (imported.status == CanonicalTypeImportStatus::Supported) {
			type.canonical_type_id = imported.type;
			transaction.commit();
		}
	}
	if (!type.template_parameter_name.isValid() && typeIndexContainsDependentPlaceholder(type_spec.type_index())) {
		const std::optional<TemplateParameterKind> parameter_kind = currentTemplateParamKind(type_spec.token().handle());
		if (parameter_kind == TemplateParameterKind::Type || parameter_kind == TemplateParameterKind::Template) {
			type.template_parameter_name = type_spec.token().handle();
		} else if (std::ranges::find(currentTemplateParamNames(), type_spec.token().handle()) != currentTemplateParamNames().end()) {
			type.template_parameter_name = type_spec.token().handle();
		} else {
			type.template_parameter_name = getStructuredTypeName(type_spec);
		}
	}
	return type;
}
