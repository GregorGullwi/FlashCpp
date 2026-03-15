#pragma once

#include "AstNodeTypes.h"
#include "SemanticTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "ChunkedString.h"
#include "TemplateTypes.h"  // For FunctionSignatureKey
#include <vector>
#include <optional>
#include <unordered_map>

// Conversion rank for overload resolution
// Lower rank = better match
enum class ConversionRank {
	ExactMatch = 0,          // No conversion needed
	Promotion = 1,           // Integral or floating-point promotion
	Conversion = 2,          // Standard conversion (int to double, etc.)
	UserDefined = 3,         // User-defined conversion via conversion operator
	NoMatch = 4              // No valid conversion
};

// Result of checking if one type can convert to another
struct TypeConversionResult {
	ConversionRank rank;
	bool is_valid;
	
	TypeConversionResult(ConversionRank r, bool valid) : rank(r), is_valid(valid) {}
	
	static TypeConversionResult exact_match() { return {ConversionRank::ExactMatch, true}; }
	static TypeConversionResult promotion() { return {ConversionRank::Promotion, true}; }
	static TypeConversionResult conversion() { return {ConversionRank::Conversion, true}; }
	static TypeConversionResult no_match() { return {ConversionRank::NoMatch, false}; }
};

// Check if a type is an integral type (includes bool, unlike is_integer_type)
inline bool is_integral_type(Type type) {
	return type == Type::Bool || is_integer_type(type);
}

// Check whether two canonical type IDs represent the same type for overload resolution
// signature matching (C++20 [over.match]).  Because all types are interned, equality of
// the handles implies equality of the descriptors — this helper exists so call sites can
// express the intent without hardcoding the == operator on CanonicalTypeId.
inline bool canonical_types_match(CanonicalTypeId a, CanonicalTypeId b) {
	return a == b;  // interned: equal IDs ⟺ equal canonical types
}

// Check if one type can be implicitly converted to another
// Returns the conversion rank
inline TypeConversionResult can_convert_type(Type from, Type to) {
	// Exact match
	if (from == to) {
		return TypeConversionResult::exact_match();
	}
	
	// Bool conversions
	if (from == Type::Bool) {
		// Bool can be promoted to int
		if (to == Type::Int) {
			return TypeConversionResult::promotion();
		}
		// Bool can be converted to any integral or floating-point type
		if (is_integral_type(to) || is_floating_point_type(to)) {
			return TypeConversionResult::conversion();
		}
	}
	
	// Integral promotions
	if (is_integral_type(from) && is_integral_type(to)) {
		int from_rank = get_integer_rank(from);
		int to_rank = get_integer_rank(to);
		
		// Promotion: smaller type to int (or larger)
		if (from_rank < 3 && to_rank >= 3) {  // 3 = rank of int
			return TypeConversionResult::promotion();
		}
		
		// Conversion: any integral type to any other integral type
		return TypeConversionResult::conversion();
	}
	
	// Floating-point promotion: float to double
	if (from == Type::Float && to == Type::Double) {
		return TypeConversionResult::promotion();
	}
	
	// Floating-point conversions
	if (is_floating_point_type(from) && is_floating_point_type(to)) {
		return TypeConversionResult::conversion();
	}
	
	// Floating-integral conversions
	if (is_integral_type(from) && is_floating_point_type(to)) {
		return TypeConversionResult::conversion();
	}
	
	if (is_floating_point_type(from) && is_integral_type(to)) {
		return TypeConversionResult::conversion();
	}
	
	// Unscoped enum to integer/floating-point promotion/conversion
	// Per [conv.prom]/4: An unscoped enum whose underlying type is int is promoted to int.
	// Per [conv.integral]: An enum can be converted to any integer type.
	if (from == Type::Enum) {
		if (to == Type::Int) {
			return TypeConversionResult::promotion();
		}
		if (is_integral_type(to) || is_floating_point_type(to)) {
			return TypeConversionResult::conversion();
		}
	}
	
	// Note: Integer to unscoped enum is NOT an implicit conversion in C++11+.
	// It requires a static_cast. Do NOT add a conversion path here.

	
	// User-defined conversions: struct-to-primitive
	// Optimistically assume conversion operator exists, CodeGen will verify
	if (from == Type::Struct && to != Type::Struct) {
		return TypeConversionResult{ConversionRank::UserDefined, true};
	}
	
	// User-defined conversions: primitive-to-struct (converting constructors)
	if (to == Type::Struct && from != Type::Struct) {
		return TypeConversionResult{ConversionRank::UserDefined, true};
	}
	
	// No valid conversion
	return TypeConversionResult::no_match();
}

// Helper function to find a conversion operator in a struct
// Returns true if a conversion operator exists from source_type to target_type
// This version searches both gTypeInfo (for CodeGen) and gSymbolTable (for Parser/overload resolution)
inline bool hasConversionOperator(TypeIndex source_type_index, Type target_type, TypeIndex target_type_index = TypeIndex{}) {
	// First, try to get struct name from gTypeInfo and search gSymbolTable
	// This is needed during parsing when gTypeInfo.member_functions is not yet populated
	if (source_type_index.is_valid() && source_type_index.value < gTypeInfo.size()) {
		const TypeInfo& source_type_info = gTypeInfo[source_type_index.value];
		std::string_view struct_name = StringTable::getStringView(source_type_info.name());
		
		// Build the target type name for the operator
		std::string_view target_type_name;
		if (target_type == Type::Struct && target_type_index.is_valid() && target_type_index.value < gTypeInfo.size()) {
			target_type_name = StringTable::getStringView(gTypeInfo[target_type_index.value].name());
		} else {
			// For primitive types, use the helper function to get the type name
			target_type_name = getTypeName(target_type);
			if (target_type_name.empty()) {
				return false;
			}
		}
		
		// Create the operator name (e.g., "operator int")
		StringBuilder sb;
		sb.append("operator ").append(target_type_name);
		std::string_view operator_name = sb.commit();
		
		// Look up the struct in gSymbolTable
		extern SymbolTable gSymbolTable;
		auto struct_symbol = gSymbolTable.lookup(StringTable::getOrInternStringHandle(struct_name));
		if (struct_symbol.has_value() && struct_symbol->is<StructDeclarationNode>()) {
			const StructDeclarationNode& struct_node = struct_symbol->template as<StructDeclarationNode>();
			
			// Search member functions in the StructDeclarationNode
			for (const auto& member_func_decl : struct_node.member_functions()) {
				const ASTNode& member_func = member_func_decl.function_declaration;
				if (member_func.template is<FunctionDeclarationNode>()) {
					const auto& func_decl = member_func.template as<FunctionDeclarationNode>();
					std::string_view func_name = func_decl.decl_node().identifier_token().value();
					if (func_name == operator_name) {
						return true;  // Found conversion operator in parsed struct
					}
				}
			}
		}
		
		// Also check gTypeInfo.member_functions (for CodeGen where it's populated)
		const StructTypeInfo* source_struct_info = source_type_info.getStructInfo();
		if (source_struct_info) {
			StringHandle operator_name_handle = StringTable::getOrInternStringHandle(operator_name);
			
			// Search member functions for the conversion operator
			for (const auto& member_func : source_struct_info->member_functions) {
				if (member_func.getName() == operator_name_handle) {
					return true;
				}
			}
			
			// Search base classes recursively
			for (const auto& base_spec : source_struct_info->base_classes) {
				if (base_spec.type_index.is_valid() && base_spec.type_index.value < gTypeInfo.size()) {
					if (hasConversionOperator(base_spec.type_index, target_type, target_type_index)) {
						return true;
					}
				}
			}
		}
	}
	
	return false;
}

// Check if one type can be implicitly converted to another (considering pointers and references)
// 
// IMPORTANT: For proper overload resolution with lvalue vs rvalue references, the caller must:
// - Set is_lvalue_reference(true) on 'from' TypeSpecifierNode for lvalue expressions (named variables, etc.)
// - Leave 'from' as non-reference for rvalue expressions (literals, temporaries, etc.)
// This distinction is critical for matching lvalue refs vs rvalue refs in overloaded functions.
inline TypeConversionResult can_convert_type(const TypeSpecifierNode& from, const TypeSpecifierNode& to) {
	// Check pointer-to-pointer compatibility FIRST
	// This handles pointer types with lvalue/rvalue flags (which indicate value category, not actual reference types)
	// Pointers with lvalue flags can still be passed to functions expecting pointer parameters
	// IMPORTANT: We use AND (not OR) here. If only one is a pointer, we fall through to allow
	// other conversions like pointer-to-integer for builtins (e.g., __builtin_va_start).
	// Using OR would break va_args since it returns no_match when from is pointer but to is not.
	if (from.is_pointer() && to.is_pointer()) {
		// Pointer depth must match
		if (from.pointer_depth() != to.pointer_depth()) {
			return TypeConversionResult::no_match();
		}

		// Resolve type aliases for both types before comparing
		// This handles cases where template parameters or typedefs resolve to the same underlying type
		// For example: CharT* (where CharT=wchar_t) should match wchar_t*
		Type from_resolved = resolve_type_alias(from.type(), from.type_index());
		Type to_resolved = resolve_type_alias(to.type(), to.type_index());

		// Helper to check if the pointed-to type is const for first-level pointers.
		// Note: pointer_levels_[0].cv_qualifier is cv on the pointer itself (e.g., T* const),
		// not on the pointee. Top-level pointer cv must not affect pointee constness.
		auto pointee_is_const = [](const TypeSpecifierNode& type_spec) -> bool {
			size_t depth = type_spec.pointer_depth();
			if (depth == 0) return false;
			if (depth == 1) {
				// Single-level pointer (e.g., const char*): pointee constness is on the base type
				return type_spec.is_const();
			}
			// Multi-level pointer (e.g., char* const*): pointee constness is on the
			// second-to-last pointer level. For depth N, the outermost pointer's pointee
			// is the type at depth N-1, whose cv is stored in pointer_levels_[depth-2].
			const auto& levels = type_spec.pointer_levels();
			return (static_cast<uint8_t>(levels[depth - 2].cv_qualifier) & static_cast<uint8_t>(CVQualifier::Const)) != 0;
		};
		
		bool from_pointee_is_const = pointee_is_const(from);
		bool to_pointee_is_const = pointee_is_const(to);
		
		// Exact type match for pointers (after resolving aliases)
		// Must also check const qualifiers to distinguish const T* from T*
		if (from_resolved == to_resolved && from_pointee_is_const == to_pointee_is_const) {
			return TypeConversionResult::exact_match();
		}
		
		// If base types match but const qualifiers differ
		if (from_resolved == to_resolved) {
			// T* → const T* is allowed (qualification conversion - adding const)
			if (!from_pointee_is_const && to_pointee_is_const) {
				return TypeConversionResult::conversion();
			}
			// const T* → T* is NOT allowed (would remove const)
			if (from_pointee_is_const && !to_pointee_is_const) {
				return TypeConversionResult::no_match();
			}
		}
		
		// If one type is still UserDefined after resolution attempt, accept as conversion
		// This allows template parameter types to match concrete types during instantiation
		// Use resolved types here to ensure that resolved typedefs still go through
		// const-correctness checks (e.g., const MyInt* → void* where MyInt is typedef for int)
		if (from_resolved == Type::UserDefined || to_resolved == Type::UserDefined) {
			// Still enforce const-correctness: const T* → T* is not allowed
			if (from_pointee_is_const && !to_pointee_is_const) {
				return TypeConversionResult::no_match();
			}
			return TypeConversionResult::conversion();
		}

		// Pointer conversions: any pointer can implicitly convert to void*
		// This is a standard C/C++ implicit conversion
		// Const-correctness rules:
		//   - const T* → const void*  : allowed (preserves const)
		//   - T*       → const void*  : allowed (adding const is safe)
		//   - const T* → void*        : REJECTED (would violate const correctness)
		//   - T*       → void*        : allowed
		// Note: For "const T*", the const applies to the pointed-to type (checked via pointee const),
		//       while "T* const" would have const on the pointer level itself.
		if (to_resolved == Type::Void) {
			// Check const-correctness for the pointed-to type
			// from_pointee_is_const checks if the pointee is const (e.g., "const char*")
			// to_pointee_is_const checks if the target pointee is const (e.g., "const void*")
			// Rule: const T* cannot convert to non-const void* (would violate const correctness)
			if (from_pointee_is_const && !to_pointee_is_const) {
				return TypeConversionResult::no_match();
			}
			// All other cases are valid: T*→void*, T*→const void*, const T*→const void*
			return TypeConversionResult::conversion();
		}

		return TypeConversionResult::no_match();
	}

	// Check reference compatibility
	if (from.is_reference() || to.is_reference()) {
		// If 'to' is a reference, 'from' must be compatible
		if (to.is_reference()) {
			// Check if both are references
			if (from.is_reference()) {
				// Both are references - check reference kind
				bool from_is_rvalue = from.is_rvalue_reference();
				bool to_is_rvalue = to.is_rvalue_reference();
				
				FLASH_LOG(Parser, Debug, "can_convert_type: both are references. from_is_rvalue=", from_is_rvalue, ", to_is_rvalue=", to_is_rvalue, ", from.type()=", (int)from.type(), ", to.type()=", (int)to.type(), ", from.type_index()=", from.type_index(), ", to.type_index()=", to.type_index());
				
				// Exact match: both lvalue ref or both rvalue ref, same base type
				Type from_base = resolve_type_alias(from.type(), from.type_index());
				Type to_base = resolve_type_alias(to.type(), to.type_index());
				if (from_is_rvalue == to_is_rvalue && from_base == to_base) {
						if ((from.is_const() && !to.is_const()) || (from.is_volatile() && !to.is_volatile())) {
							return TypeConversionResult::no_match();
						}
					return TypeConversionResult::exact_match();
				}
				
				// Lvalue ref can't bind to rvalue ref parameter
				// Rvalue ref can't bind to lvalue ref parameter  
				return TypeConversionResult::no_match();
			} else {
				// 'from' is not a reference, 'to' is a reference
				// Handle binding of non-references to reference parameters
				
				bool to_is_rvalue = to.is_rvalue_reference();
				bool to_is_const = to.is_const();
				
				// Check if base types are compatible (resolve aliases like char_type → wchar_t)
				Type from_base = resolve_type_alias(from.type(), from.type_index());
				Type to_base = resolve_type_alias(to.type(), to.type_index());
				bool types_match = (from_base == to_base);
				if (!types_match) {
					// Allow conversions for const lvalue refs only
					auto conversion = can_convert_type(from_base, to_base);
					if (!to_is_rvalue && to_is_const && conversion.is_valid) {
						// Const lvalue ref can bind to values that can be converted
						return conversion;
					}
					return TypeConversionResult::no_match();
				}
				
				if (to_is_rvalue) {
					// Rvalue reference can bind to temporaries (prvalues)
					// Non-reference values are treated as rvalues when passed
					return TypeConversionResult::exact_match();
				} else {
					// Lvalue reference
					if (to_is_const) {
						// Const lvalue ref can bind to both lvalues and rvalues
						return TypeConversionResult::exact_match();
					} else {
						// Non-const lvalue ref can only bind to lvalues
						// In this context, 'from' is not marked as a reference, indicating
						// it represents the value category of a non-lvalue expression (rvalue)
						// Note: The caller must set is_lvalue_reference on 'from' for actual lvalue expressions
						return TypeConversionResult::no_match();
					}
				}
			}
		} else {
			// 'from' is a reference, 'to' is not
			// References can be converted to their base type (automatic dereferencing)
			// When copying through a reference, const qualifiers don't matter
			// (e.g., const T& can be copied to T)
			
			// Resolve type aliases before comparing (e.g., char_type → wchar_t)
			Type from_resolved = resolve_type_alias(from.type(), from.type_index());
			Type to_resolved = resolve_type_alias(to.type(), to.type_index());
			
			if (from_resolved == to_resolved) {
				return TypeConversionResult::exact_match();
			}
			// If one type is still UserDefined after resolution attempt, accept as conversion
			// This handles unresolved template parameter type aliases
			if (from_resolved == Type::UserDefined || to_resolved == Type::UserDefined) {
				return TypeConversionResult::conversion();
			}
			// Try conversion of the referenced type to target type
			return can_convert_type(from_resolved, to_resolved);
		}
	}

	// Check for user-defined conversion operators
	// If 'from' is a struct type and 'to' is a different type, assume conversion might be possible
	// The actual conversion operator existence will be checked during CodeGen
	if (from.type() == Type::Struct && to.type() != Type::Struct) {
		// For struct-to-primitive conversions, optimistically assume a conversion operator exists
		// CodeGen will verify and generate the actual call
		return TypeConversionResult{ConversionRank::UserDefined, true};
	}

	// Check for user-defined conversions in reverse: if 'to' is Struct and 'from' is not
	// This handles constructor conversions (not conversion operators, but similar concept)
	if (to.type() == Type::Struct && from.type() != Type::Struct) {
		// Could be a converting constructor in 'to' struct - accept it tentatively
		// CodeGen will handle the actual constructor call
		return TypeConversionResult{ConversionRank::UserDefined, true};
	}

	// Handle UserDefined type aliases: 
	// Type aliases like 'size_t' may be stored as Type::UserDefined with type_index=0
	// when they couldn't be fully resolved during parsing. Allow conversions between
	// UserDefined and integral types as they're likely type aliases for integral types.
	Type from_type = resolve_type_alias(from.type(), from.type_index());
	Type to_type = resolve_type_alias(to.type(), to.type_index());
	
	// If either type is still UserDefined with type_index=0, assume it's an unresolved type alias
	// Allow conversion if the other type is an integral type (common for size_t, ptrdiff_t, etc.)
	if (from_type == Type::UserDefined && !from.type_index().is_valid()) {
		// 'from' is an unresolved type alias - allow if 'to' is integral
		if (is_integral_type(to_type)) {
			return TypeConversionResult::conversion();
		}
	}
	if (to_type == Type::UserDefined && !to.type_index().is_valid()) {
		// 'to' is an unresolved type alias - allow if 'from' is integral
		if (is_integral_type(from_type)) {
			return TypeConversionResult::conversion();
		}
	}

	// Non-pointer, non-reference types: use basic type conversion with resolved types
	return can_convert_type(from_type, to_type);
}

// Result of overload resolution
struct OverloadResolutionResult {
	const ASTNode* selected_overload = nullptr;
	bool is_ambiguous = false;
	bool has_match = false;
	
	OverloadResolutionResult() = default;
	OverloadResolutionResult(const ASTNode* overload) 
		: selected_overload(overload), is_ambiguous(false), has_match(true) {}
	
	static OverloadResolutionResult ambiguous() {
		OverloadResolutionResult result;
		result.is_ambiguous = true;
		return result;
	}
	
	static OverloadResolutionResult no_match() {
		return OverloadResolutionResult();
	}
};

struct ConstructorOverloadResolutionResult {
	const ConstructorDeclarationNode* selected_overload = nullptr;
	bool is_ambiguous = false;
	bool has_match = false;

	ConstructorOverloadResolutionResult() = default;
	explicit ConstructorOverloadResolutionResult(const ConstructorDeclarationNode* overload)
		: selected_overload(overload), is_ambiguous(false), has_match(overload != nullptr) {}

	static ConstructorOverloadResolutionResult ambiguous() {
		ConstructorOverloadResolutionResult result;
		result.is_ambiguous = true;
		return result;
	}

	static ConstructorOverloadResolutionResult no_match() {
		return ConstructorOverloadResolutionResult();
	}
};

inline bool is_lvalue_expression_for_overload_resolution(const ASTNode& arg_node) {
	if (!arg_node.is<ExpressionNode>()) {
		return false;
	}

	const ExpressionNode& arg_expr = arg_node.as<ExpressionNode>();
	return std::visit([](const auto& inner) -> bool {
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
	}, arg_expr);
}

inline void adjust_argument_type_for_overload_resolution(const ASTNode& arg_node, TypeSpecifierNode& arg_type) {
	if (is_lvalue_expression_for_overload_resolution(arg_node)) {
		arg_type.set_reference_qualifier(ReferenceQualifier::LValueReference);
	}
}

inline size_t countMinRequiredArgs(const ConstructorDeclarationNode& ctor) {
	const auto& params = ctor.parameter_nodes();
	size_t min_required = params.size();
	size_t i = params.size();

	while (i > 0) {
		if (!params[i - 1].is<DeclarationNode>()) {
			break;
		}
		const auto& param_decl = params[i - 1].as<DeclarationNode>();
		if (!param_decl.has_default_value()) {
			break;
		}
		min_required--;
		--i;
	}

	return min_required;
}

inline ConstructorOverloadResolutionResult resolve_constructor_overload(
	const StructTypeInfo& struct_info,
	const std::vector<TypeSpecifierNode>& argument_types,
	bool skip_implicit = false)
{
	const ConstructorDeclarationNode* best_match = nullptr;
	std::vector<ConversionRank> best_ranks;
	int num_best_matches = 0;
	std::vector<const ConstructorDeclarationNode*> tied_candidates;

	for (const auto& member_func : struct_info.member_functions) {
		if (!member_func.is_constructor || !member_func.function_decl.is<ConstructorDeclarationNode>()) {
			continue;
		}

		const auto& ctor_decl = member_func.function_decl.as<ConstructorDeclarationNode>();
		if (skip_implicit && ctor_decl.is_implicit()) {
			continue;
		}

		const auto& parameters = ctor_decl.parameter_nodes();
		size_t min_required = countMinRequiredArgs(ctor_decl);
		if (argument_types.size() < min_required || argument_types.size() > parameters.size()) {
			continue;
		}

		if (ctor_decl.is_implicit() && parameters.size() == 1 && argument_types.size() == 1 &&
			parameters[0].is<DeclarationNode>()) {
			const auto& param_type_node = parameters[0].as<DeclarationNode>().type_node();
			if (param_type_node.is<TypeSpecifierNode>()) {
				const auto& param_type = param_type_node.as<TypeSpecifierNode>();
				if ((param_type.is_reference() || param_type.is_rvalue_reference()) &&
					is_struct_type(param_type.type()) && struct_info.own_type_index_.has_value()) {
					const TypeSpecifierNode& arg_type = argument_types[0];
					Type resolved_arg_type = resolve_type_alias(arg_type.type(), arg_type.type_index());
					bool is_same_struct_type = is_struct_type(resolved_arg_type) &&
						arg_type.type_index() == *struct_info.own_type_index_;
					if (!is_same_struct_type) {
						continue;
					}
				}
			}
		}

		std::vector<ConversionRank> conversion_ranks;
		bool all_convertible = true;
		for (size_t i = 0; i < argument_types.size(); ++i) {
			if (!parameters[i].is<DeclarationNode>() || !parameters[i].as<DeclarationNode>().type_node().is<TypeSpecifierNode>()) {
				all_convertible = false;
				break;
			}

			const auto& param_type = parameters[i].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			auto conversion = can_convert_type(argument_types[i], param_type);
			if (!conversion.is_valid) {
				all_convertible = false;
				break;
			}
			conversion_ranks.push_back(conversion.rank);
		}

		if (!all_convertible) {
			continue;
		}

		if (!best_match) {
			best_match = &ctor_decl;
			best_ranks = conversion_ranks;
			num_best_matches = 1;
			tied_candidates.clear();
			tied_candidates.push_back(&ctor_decl);
			continue;
		}

		bool this_is_better = false;
		bool this_is_worse = false;
		for (size_t i = 0; i < conversion_ranks.size(); ++i) {
			if (conversion_ranks[i] < best_ranks[i]) {
				this_is_better = true;
			} else if (conversion_ranks[i] > best_ranks[i]) {
				this_is_worse = true;
			}
		}

		if (this_is_better && !this_is_worse) {
			best_match = &ctor_decl;
			best_ranks = conversion_ranks;
			num_best_matches = 1;
			tied_candidates.clear();
			tied_candidates.push_back(&ctor_decl);
		} else if (!this_is_better && !this_is_worse) {
			num_best_matches++;
			tied_candidates.push_back(&ctor_decl);
		}
	}

	if (!best_match) {
		return ConstructorOverloadResolutionResult::no_match();
	}

	if (num_best_matches > 1) {
		return ConstructorOverloadResolutionResult::ambiguous();
	}

	return ConstructorOverloadResolutionResult(best_match);
}

// countMinRequiredArgs is defined in SymbolTable.h (included above)

// Perform overload resolution for a function call
// Returns the best matching overload, or nullptr if no match or ambiguous
inline OverloadResolutionResult resolve_overload(
	const std::vector<ASTNode>& overloads,
	const std::vector<TypeSpecifierNode>& argument_types)
{
	if (overloads.empty()) {
		return OverloadResolutionResult::no_match();
	}
	
	// Track the best match found so far
	const ASTNode* best_match = nullptr;
	std::vector<ConversionRank> best_ranks;
	int num_best_matches = 0;
	std::vector<const ASTNode*> tied_candidates;  // All candidates with best rank
	
	// Evaluate each overload
	for (const auto& overload : overloads) {
		// Extract the function declaration
		const FunctionDeclarationNode* func_decl = nullptr;
		if (overload.is<FunctionDeclarationNode>()) {
			func_decl = &overload.as<FunctionDeclarationNode>();
		} else {
			// Not a function declaration, skip it
			continue;
		}

		// Check parameter count
		const auto& parameters = func_decl->parameter_nodes();
		bool is_variadic = func_decl->is_variadic();

		// For variadic functions, we need at least as many arguments as named parameters
		// For non-variadic functions, argument count must be between min required and total params
		size_t min_required = countMinRequiredArgs(*func_decl);
		if (is_variadic) {
			if (argument_types.size() < min_required) {
				continue;  // Too few arguments for variadic function
			}
		} else {
			if (argument_types.size() < min_required || argument_types.size() > parameters.size()) {
				continue;  // Argument count mismatch (accounting for default arguments)
			}
		}
		
		// Check if all provided arguments can be converted to parameters
		// For variadic functions, only check the named parameters
		// The variadic arguments (...) accept any type
		std::vector<ConversionRank> conversion_ranks;
		bool all_convertible = true;

		size_t params_to_check = std::min(parameters.size(), argument_types.size());

		for (size_t i = 0; i < params_to_check; ++i) {
			const auto& param_type = parameters[i].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
			const auto& arg_type = argument_types[i];

			auto conversion = can_convert_type(arg_type, param_type);
			if (!conversion.is_valid) {
				all_convertible = false;
				break;
			}
			conversion_ranks.push_back(conversion.rank);
		}

		// For variadic functions, the extra arguments beyond named parameters
		// are considered to have "exact match" rank (they're accepted as-is)
		if (is_variadic) {
			for (size_t i = params_to_check; i < argument_types.size(); ++i) {
				conversion_ranks.push_back(ConversionRank::ExactMatch);
			}
		}

		if (!all_convertible) {
			continue;  // This overload doesn't match
		}
		
		// Compare with the best match so far
		if (best_match == nullptr) {
			// First valid match
			best_match = &overload;
			best_ranks = conversion_ranks;
			num_best_matches = 1;
			tied_candidates.clear();
			tied_candidates.push_back(&overload);
		} else {
			// Compare conversion ranks
			bool this_is_better = false;
			bool this_is_worse = false;
			
			for (size_t i = 0; i < conversion_ranks.size(); ++i) {
				if (conversion_ranks[i] < best_ranks[i]) {
					this_is_better = true;
				} else if (conversion_ranks[i] > best_ranks[i]) {
					this_is_worse = true;
				}
			}
			
			if (this_is_better && !this_is_worse) {
				// This overload is strictly better
				best_match = &overload;
				best_ranks = conversion_ranks;
				num_best_matches = 1;
				tied_candidates.clear();
				tied_candidates.push_back(&overload);
			} else if (!this_is_better && !this_is_worse) {
				// This overload is equally good - ambiguous
				num_best_matches++;
				tied_candidates.push_back(&overload);
			}
			// If this_is_worse, ignore this overload
		}
	}
	
	if (best_match == nullptr) {
		return OverloadResolutionResult::no_match();
	}
	
	if (num_best_matches > 1) {
		// Check if all tied candidates differ only in cv-qualification (const/volatile)
		// on their parameters. FlashCpp doesn't fully track volatile qualifiers, so
		// overloads like f(T*) vs f(volatile T*) score identically. In that case,
		// prefer the first declared overload rather than reporting ambiguity.
		bool differs_only_in_cv = true;
		const FunctionDeclarationNode* best_func = &best_match->as<FunctionDeclarationNode>();
		for (const auto* candidate : tied_candidates) {
			if (candidate == best_match) continue;
			const FunctionDeclarationNode* cand_func = &candidate->as<FunctionDeclarationNode>();
			const auto& best_params = best_func->parameter_nodes();
			const auto& cand_params = cand_func->parameter_nodes();
			if (best_params.size() != cand_params.size()) {
				differs_only_in_cv = false;
				break;
			}
			for (size_t i = 0; i < best_params.size(); ++i) {
				const auto& bp = best_params[i].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				const auto& cp = cand_params[i].as<DeclarationNode>().type_node().as<TypeSpecifierNode>();
				// If base types and pointer depths match, they differ only in cv-qualification
				if (bp.type() != cp.type() || bp.type_index() != cp.type_index() ||
				    bp.pointer_depth() != cp.pointer_depth() ||
				    bp.is_reference() != cp.is_reference() ||
				    bp.is_rvalue_reference() != cp.is_rvalue_reference()) {
					differs_only_in_cv = false;
					break;
				}
			}
			if (!differs_only_in_cv) break;
		}
		
		if (differs_only_in_cv) {
			// Candidates differ only in cv-qualification — prefer the first match
			return OverloadResolutionResult(best_match);
		}
		return OverloadResolutionResult::ambiguous();
	}
	
	return OverloadResolutionResult(best_match);
}

// Result of operator overload resolution
struct OperatorOverloadResult {
	const StructMemberFunction* member_overload = nullptr;
	const FunctionDeclarationNode* free_function_overload = nullptr;  // For free-function operators
	bool is_ambiguous = false;
	bool has_match = false;
	bool is_free_function = false;  // True when free_function_overload is the active match
	
	OperatorOverloadResult() = default;
	explicit OperatorOverloadResult(const StructMemberFunction* overload)
		: member_overload(overload), has_match(overload != nullptr) {}
	explicit OperatorOverloadResult(const FunctionDeclarationNode* free_func)
		: free_function_overload(free_func), has_match(free_func != nullptr), is_free_function(free_func != nullptr) {}

	static OperatorOverloadResult ambiguous() {
		OperatorOverloadResult result;
		result.is_ambiguous = true;
		return result;
	}

	static OperatorOverloadResult no_match() {
		return OperatorOverloadResult();
	}
	
	static OperatorOverloadResult no_overload() {
		return no_match();
	}
};

// Helper: resolve a struct parameter type_index for self-referential template parameters.
// When a template struct Foo<T> is instantiated as Foo<int>, the member function
// operator+=(const Foo& other) stores the parameter as the uninstantiated Foo
// (whose struct_info has total_size==0). We resolve it to the concrete instantiated
// left_type_index so that type matching works correctly.
// This mirrors the AstToIr::resolveSelfReferentialType logic used in codegen.
inline TypeIndex resolveSelfRefParamIndex(TypeIndex param_idx, TypeIndex left_type_index) {
	const size_t type_info_size = gTypeInfo.size();
	if (!param_idx.is_valid() || param_idx.value >= type_info_size || left_type_index.value >= type_info_size) return param_idx;
	const auto& param_ti = gTypeInfo[param_idx.value];
	if (!param_ti.struct_info_ || param_ti.struct_info_->total_size != 0) return param_idx;
	// param refers to an uninstantiated template (total_size==0); check name family
	auto template_base_name = StringTable::getStringView(param_ti.name());
	auto instantiated_name = StringTable::getStringView(gTypeInfo[left_type_index.value].name());
	// Strip template hash suffix from the instantiated name: "Name$hash" -> "Name"
	auto base_name = instantiated_name;
	auto dollar_pos = base_name.find('$');
	if (dollar_pos != std::string_view::npos) {
		base_name = base_name.substr(0, dollar_pos);
	}
	return (template_base_name == base_name) ? left_type_index : param_idx;
}

inline bool binaryOperatorUsesTypeIndexIdentity(Type type) {
	return type == Type::Struct || type == Type::Enum || type == Type::UserDefined;
}

inline Type effectiveBinaryOperatorTypeFromSpec(const TypeSpecifierNode& spec) {
	Type type = spec.type();
	if ((type == Type::Invalid || type == Type::Void) && spec.type_index().is_valid() && spec.type_index().value < gTypeInfo.size()) {
		type = gTypeInfo[spec.type_index().value].type_;
	}
	if ((type == Type::Invalid || type == Type::Void) && spec.type_index().is_valid()) {
		return Type::Struct;
	}
	return type;
}

inline bool isConcreteBinaryOperatorOperandType(const TypeSpecifierNode& spec) {
	Type type = effectiveBinaryOperatorTypeFromSpec(spec);
	if (type == Type::Invalid || type == Type::Void) {
		return false;
	}
	if (binaryOperatorUsesTypeIndexIdentity(type)) {
		return spec.type_index().is_valid();
	}
	return true;
}

inline bool isUserDefinedBinaryOperatorOperandType(const TypeSpecifierNode& spec) {
	if (spec.pointer_depth() > 0
		|| spec.is_function_pointer()
		|| spec.is_member_function_pointer()
		|| spec.is_member_object_pointer()) {
		return false;
	}
	Type type = effectiveBinaryOperatorTypeFromSpec(spec);
	return binaryOperatorUsesTypeIndexIdentity(type) && spec.type_index().is_valid();
}

inline TypeSpecifierNode makeBinaryOperatorTypeSpecifier(Type type, TypeIndex type_index) {
	Type effective_type = type;
	int size_bits = 0;

	if (type_index.is_valid() && type_index.value < gTypeInfo.size()) {
		const auto& type_info = gTypeInfo[type_index.value];
		if (effective_type == Type::Invalid || effective_type == Type::Void || binaryOperatorUsesTypeIndexIdentity(effective_type)) {
			if (type_info.type_ != Type::Invalid && type_info.type_ != Type::Void) {
				effective_type = type_info.type_;
			} else if (effective_type == Type::Invalid || effective_type == Type::Void) {
				effective_type = Type::Struct;
			}
		}

		if (const StructTypeInfo* struct_info = type_info.getStructInfo()) {
			size_bits = static_cast<int>(struct_info->total_size * 8);
		} else if (type_info.type_size_ > 0) {
			size_bits = type_info.type_size_;
		}
	}

	if (size_bits == 0 && effective_type != Type::Invalid && effective_type != Type::Void) {
		size_bits = get_type_size_bits(effective_type);
	}

	if (binaryOperatorUsesTypeIndexIdentity(effective_type) || type_index.is_valid()) {
		return TypeSpecifierNode(effective_type, type_index, size_bits);
	}

	return TypeSpecifierNode(effective_type, TypeQualifier::None, size_bits);
}

inline TypeSpecifierNode resolveBinaryOperatorTypeForSelfReference(const TypeSpecifierNode& type_spec, TypeIndex enclosing_type_index) {
	TypeSpecifierNode resolved = type_spec;
	Type resolved_type = effectiveBinaryOperatorTypeFromSpec(resolved);
	if (binaryOperatorUsesTypeIndexIdentity(resolved_type)) {
		resolved.set_type_index(resolveSelfRefParamIndex(resolved.type_index(), enclosing_type_index));
	}
	return resolved;
}

inline ConversionRank rankBinaryOperatorOperandMatch(const TypeSpecifierNode& arg_spec, const TypeSpecifierNode& param_spec, TypeIndex enclosing_type_index) {
	TypeSpecifierNode resolved_param_spec = resolveBinaryOperatorTypeForSelfReference(param_spec, enclosing_type_index);
	if (isUserDefinedBinaryOperatorOperandType(arg_spec)
		&& isUserDefinedBinaryOperatorOperandType(resolved_param_spec)
		&& arg_spec.type_index().is_valid()
		&& resolved_param_spec.type_index().is_valid()
		&& arg_spec.type_index() != resolved_param_spec.type_index()) {
		return ConversionRank::NoMatch;
	}
	auto conversion = can_convert_type(arg_spec, resolved_param_spec);
	return conversion.is_valid ? conversion.rank : ConversionRank::NoMatch;
}

inline ConversionRank rankImplicitObjectToBinaryOperator(
	const TypeSpecifierNode& object_spec,
	const StructMemberFunction& member_func,
	TypeIndex actual_object_type_index,
	TypeIndex member_owner_type_index)
{
	if (object_spec.is_const() && !member_func.is_const()) {
		return ConversionRank::NoMatch;
	}
	if (object_spec.is_volatile() && !member_func.is_volatile()) {
		return ConversionRank::NoMatch;
	}

	bool uses_base_member = actual_object_type_index.is_valid() && member_owner_type_index.is_valid()
		&& actual_object_type_index != member_owner_type_index;

	if (uses_base_member) {
		return ConversionRank::Conversion;
	}

	return ConversionRank::ExactMatch;
}

enum class BinaryOperatorCandidateComparison {
	Better,
	Worse,
	Equivalent,
	Incomparable,
};

inline BinaryOperatorCandidateComparison compareBinaryOperatorCandidateRanks(
	ConversionRank lhs_lhs_rank,
	ConversionRank lhs_rhs_rank,
	ConversionRank rhs_lhs_rank,
	ConversionRank rhs_rhs_rank)
{
	bool lhs_is_better = false;
	bool lhs_is_worse = false;

	if (lhs_lhs_rank < rhs_lhs_rank) lhs_is_better = true;
	else if (lhs_lhs_rank > rhs_lhs_rank) lhs_is_worse = true;

	if (lhs_rhs_rank < rhs_rhs_rank) lhs_is_better = true;
	else if (lhs_rhs_rank > rhs_rhs_rank) lhs_is_worse = true;

	if (lhs_is_better && !lhs_is_worse) return BinaryOperatorCandidateComparison::Better;
	if (!lhs_is_better && lhs_is_worse) return BinaryOperatorCandidateComparison::Worse;
	if (!lhs_is_better && !lhs_is_worse) return BinaryOperatorCandidateComparison::Equivalent;
	return BinaryOperatorCandidateComparison::Incomparable;
}

// Find operator overload in a struct type
// Returns the member function that overloads the given operator, or nullptr if not found
inline OperatorOverloadResult findUnaryOperatorOverload(TypeIndex operand_type_index, OverloadableOperator operator_kind) {
	// Only struct types can have operator overloads
	if (!operand_type_index.is_valid() || operand_type_index.value >= gTypeInfo.size()) {
		return OperatorOverloadResult::no_overload();
	}
	
	const TypeInfo& type_info = gTypeInfo[operand_type_index.value];
	const StructTypeInfo* struct_info = type_info.getStructInfo();
	
	if (!struct_info) {
		return OperatorOverloadResult::no_overload();
	}
	
	// Search for the operator overload in member functions
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.operator_kind == operator_kind) {
			return OperatorOverloadResult(&member_func);
		}
	}
	
	// Search base classes recursively
	for (const auto& base_spec : struct_info->base_classes) {
		if (base_spec.type_index.is_valid() && base_spec.type_index.value < gTypeInfo.size()) {
			auto result = findUnaryOperatorOverload(base_spec.type_index, operator_kind);
			if (result.has_match || result.is_ambiguous) {
				return result;
			}
		}
	}
	
	return OperatorOverloadResult::no_overload();
}

// Find binary operator overload in a struct type (member function form)
// For binary operators like operator+, operator-, etc.
// Returns the member function that overloads the given operator, or nullptr if not found
// This handles the member function form: a.operator+(b)
inline OperatorOverloadResult findBinaryOperatorOverload(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind)
{
	TypeIndex left_type_index = left_type_spec.type_index();
	if (!left_type_index.is_valid() || left_type_index.value >= gTypeInfo.size()) {
		return OperatorOverloadResult::no_overload();
	}

	const StructTypeInfo* left_struct_info = gTypeInfo[left_type_index.value].getStructInfo();
	if (!left_struct_info) {
		return OperatorOverloadResult::no_overload();
	}

	struct OperatorCandidate {
		ConversionRank lhs_rank;
		ConversionRank rhs_rank;
		const StructMemberFunction* member_func = nullptr;
	};
	std::vector<OperatorCandidate> candidates;

	auto gatherMemberCandidates = [&](auto& self, TypeIndex struct_idx) -> void {
		if (!struct_idx.is_valid() || struct_idx.value >= gTypeInfo.size()) return;
		const StructTypeInfo* si = gTypeInfo[struct_idx.value].getStructInfo();
		if (!si) return;

		for (const auto& member_func : si->member_functions) {
			if (operator_kind == OverloadableOperator::Assign) {
				if (!isAssignOperator(member_func.operator_kind)) continue;
			} else if (member_func.operator_kind != operator_kind) {
				continue;
			}

			if (!member_func.function_decl.is<FunctionDeclarationNode>()) continue;

			const auto& params = member_func.function_decl.as<FunctionDeclarationNode>().parameter_nodes();
			if (params.size() != 1 || !params[0].is<DeclarationNode>()) continue;

			const auto& param_type_node = params[0].as<DeclarationNode>().type_node();
			if (!param_type_node.is<TypeSpecifierNode>()) continue;

		ConversionRank lhs_rank = rankImplicitObjectToBinaryOperator(
			left_type_spec,
			member_func,
			left_type_index,
			struct_idx);
		if (lhs_rank == ConversionRank::NoMatch) continue;

			ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(
				right_type_spec,
				param_type_node.as<TypeSpecifierNode>(),
				struct_idx);
			if (rhs_rank == ConversionRank::NoMatch) continue;

			candidates.push_back({lhs_rank, rhs_rank, &member_func});
		}

		for (const auto& base_spec : si->base_classes) {
			if (base_spec.type_index.is_valid() && base_spec.type_index.value < gTypeInfo.size()) {
				self(self, base_spec.type_index);
			}
		}
	};
	gatherMemberCandidates(gatherMemberCandidates, left_type_index);

	if (candidates.empty()) {
		return OperatorOverloadResult::no_overload();
	}

	std::vector<const OperatorCandidate*> best_candidates;
	best_candidates.reserve(candidates.size());

	for (size_t i = 0; i < candidates.size(); ++i) {
		const auto& candidate = candidates[i];
		bool is_dominated = false;

		for (size_t j = 0; j < candidates.size(); ++j) {
			if (i == j) continue;
			if (compareBinaryOperatorCandidateRanks(
					candidates[j].lhs_rank,
					candidates[j].rhs_rank,
					candidate.lhs_rank,
					candidate.rhs_rank) == BinaryOperatorCandidateComparison::Better) {
				is_dominated = true;
				break;
			}
		}

		if (!is_dominated) {
			best_candidates.push_back(&candidate);
		}
	}

	if (best_candidates.empty()) {
		return OperatorOverloadResult::no_match();
	}

	if (best_candidates.size() != 1) {
		return OperatorOverloadResult::ambiguous();
	}

	return OperatorOverloadResult(best_candidates[0]->member_func);
}

inline OperatorOverloadResult findBinaryOperatorOverload(TypeIndex left_type_index, TypeIndex right_type_index, OverloadableOperator operator_kind, Type right_type) {
	Type effective_right_type = right_type;
	if (right_type_index.is_valid() && right_type_index.value < gTypeInfo.size()) {
		Type indexed_right_type = resolve_type_alias(gTypeInfo[right_type_index.value].type_, right_type_index);
		if (binaryOperatorUsesTypeIndexIdentity(indexed_right_type)) {
			effective_right_type = Type::Invalid;
		}
	}
	return findBinaryOperatorOverload(
		makeBinaryOperatorTypeSpecifier(Type::Invalid, left_type_index),
		makeBinaryOperatorTypeSpecifier(effective_right_type, right_type_index),
		operator_kind);
}

// Find binary operator overload, including free-function operators in the given symbol table.
// Per C++20 [over.match.oper]/2, both member and non-member candidates are collected into
// a single candidate set and ranked together per [over.best.ics] and [over.match.best].
// When a member and non-member have identical conversion ranks on all positions,
// the member is preferred per [over.match.oper]/3.3.
inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	const TypeSpecifierNode& left_type_spec,
	const TypeSpecifierNode& right_type_spec,
	OverloadableOperator operator_kind,
	std::string_view operator_symbol,
	const SymbolTable& symbol_table)
{
	// --- Unified candidate set per C++20 [over.match.oper]/2 ---
	struct OperatorCandidate {
		ConversionRank lhs_rank;
		ConversionRank rhs_rank;
		const StructMemberFunction* member_func = nullptr;
		const FunctionDeclarationNode* free_func = nullptr;
		bool is_free_function = false;
	};
	std::vector<OperatorCandidate> candidates;

	TypeIndex left_type_index = left_type_spec.type_index();
	TypeIndex right_type_index = right_type_spec.type_index();
	const size_t type_info_size = gTypeInfo.size();

	// --- 1. Gather member-function candidates (recursive through base classes) ---
	// Uses self-referencing lambda pattern to avoid std::function overhead.
	auto gatherMemberCandidates = [&](auto& self, TypeIndex struct_idx) -> void {
		if (!struct_idx.is_valid() || struct_idx.value >= type_info_size) return;
		const StructTypeInfo* si = gTypeInfo[struct_idx.value].getStructInfo();
		if (!si) return;

		for (const auto& member_func : si->member_functions) {
			if (operator_kind == OverloadableOperator::Assign) {
				if (!isAssignOperator(member_func.operator_kind)) continue;
			} else {
				if (member_func.operator_kind != operator_kind) continue;
			}

			if (!member_func.function_decl.is<FunctionDeclarationNode>()) continue;
			const auto& params = member_func.function_decl.as<FunctionDeclarationNode>().parameter_nodes();
			if (params.size() != 1 || !params[0].is<DeclarationNode>()) continue;
			const auto& param_type_node = params[0].as<DeclarationNode>().type_node();
			if (!param_type_node.is<TypeSpecifierNode>()) continue;

			ConversionRank lhs_rank = rankImplicitObjectToBinaryOperator(
				left_type_spec,
				member_func,
				left_type_index,
				struct_idx);
			if (lhs_rank == ConversionRank::NoMatch) continue;

			ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(
				right_type_spec,
				param_type_node.as<TypeSpecifierNode>(),
				struct_idx);
			if (rhs_rank != ConversionRank::NoMatch) {
				candidates.push_back({lhs_rank, rhs_rank, &member_func, nullptr, false});
			}
		}

		// Recurse into base classes
		for (const auto& base_spec : si->base_classes) {
			if (base_spec.type_index.is_valid() && base_spec.type_index.value < type_info_size) {
				self(self, base_spec.type_index);
			}
		}
	};
	gatherMemberCandidates(gatherMemberCandidates, left_type_index);

	StringBuilder op_name_sb;
	op_name_sb.append("operator").append(operator_symbol);
	std::string_view op_func_name = op_name_sb.commit();
	auto overloads = symbol_table.lookup_all(op_func_name);

	for (const auto& overload : overloads) {
		if (!overload.is<FunctionDeclarationNode>()) continue;
		const auto& func_decl = overload.as<FunctionDeclarationNode>();
		const auto& params = func_decl.parameter_nodes();
		if (params.size() < 2) continue;

		if (!params[0].is<DeclarationNode>()) continue;
		const auto& p0_type = params[0].as<DeclarationNode>().type_node();
		if (!p0_type.is<TypeSpecifierNode>()) continue;
		const auto& p0_spec = p0_type.as<TypeSpecifierNode>();

		if (!params[1].is<DeclarationNode>()) continue;
		const auto& p1_type = params[1].as<DeclarationNode>().type_node();
		if (!p1_type.is<TypeSpecifierNode>()) continue;
		const auto& p1_spec = p1_type.as<TypeSpecifierNode>();

		ConversionRank lhs_rank = rankBinaryOperatorOperandMatch(left_type_spec, p0_spec, left_type_index);
		if (lhs_rank == ConversionRank::NoMatch) continue;

		ConversionRank rhs_rank = rankBinaryOperatorOperandMatch(right_type_spec, p1_spec, right_type_index);
		if (rhs_rank == ConversionRank::NoMatch) continue;

		candidates.push_back({lhs_rank, rhs_rank, nullptr, &func_decl, true});
	}

	// --- 3. Rank all candidates per [over.match.best]/2 ---
	if (candidates.empty()) {
		return OperatorOverloadResult::no_overload();
	}

	std::vector<const OperatorCandidate*> best_candidates;
	best_candidates.reserve(candidates.size());

	for (size_t i = 0; i < candidates.size(); ++i) {
		const auto& candidate = candidates[i];
		bool is_dominated = false;

		for (size_t j = 0; j < candidates.size(); ++j) {
			if (i == j) continue;
			if (compareBinaryOperatorCandidateRanks(
					candidates[j].lhs_rank,
					candidates[j].rhs_rank,
					candidate.lhs_rank,
					candidate.rhs_rank) == BinaryOperatorCandidateComparison::Better) {
				is_dominated = true;
				break;
			}
		}

		if (!is_dominated) {
			best_candidates.push_back(&candidate);
		}
	}

	if (best_candidates.empty()) {
		return OperatorOverloadResult::no_match();
	}

	std::vector<const OperatorCandidate*> filtered_best_candidates;
	filtered_best_candidates.reserve(best_candidates.size());

	for (const OperatorCandidate* candidate : best_candidates) {
		bool loses_member_tiebreak = false;
		if (candidate->is_free_function) {
			for (const OperatorCandidate* other : best_candidates) {
				if (other->is_free_function) continue;
				if (compareBinaryOperatorCandidateRanks(
						candidate->lhs_rank,
						candidate->rhs_rank,
						other->lhs_rank,
						other->rhs_rank) == BinaryOperatorCandidateComparison::Equivalent) {
					loses_member_tiebreak = true;
					break;
				}
			}
		}

		if (!loses_member_tiebreak) {
			filtered_best_candidates.push_back(candidate);
		}
	}

	if (filtered_best_candidates.size() != 1) {
		return OperatorOverloadResult::ambiguous();
	}

	const OperatorCandidate* best = filtered_best_candidates[0];

	// Return the winner
	if (best->is_free_function) {
		return OperatorOverloadResult(best->free_func);
	} else {
		return OperatorOverloadResult(best->member_func);
	}
}

inline OperatorOverloadResult findBinaryOperatorOverloadWithFreeFunction(
	TypeIndex left_type_index,
	TypeIndex right_type_index,
	OverloadableOperator operator_kind,
	std::string_view operator_symbol,
	const SymbolTable& symbol_table,
	Type right_type)
{
	Type effective_right_type = right_type;
	if (right_type_index.is_valid() && right_type_index.value < gTypeInfo.size()) {
		Type indexed_right_type = resolve_type_alias(gTypeInfo[right_type_index.value].type_, right_type_index);
		if (binaryOperatorUsesTypeIndexIdentity(indexed_right_type)) {
			effective_right_type = Type::Invalid;
		}
	}
	return findBinaryOperatorOverloadWithFreeFunction(
		makeBinaryOperatorTypeSpecifier(Type::Invalid, left_type_index),
		makeBinaryOperatorTypeSpecifier(effective_right_type, right_type_index),
		operator_kind,
		operator_symbol,
		symbol_table);
}

// ============================================================================
// TypeIndex-based Function Signature Utilities (Phase 3)
// ============================================================================

/**
 * Create a TypeIndexArg from a TypeSpecifierNode
 * 
 * Extracts type information into a compact TypeIndexArg for fast comparison
 * and hashing in function signature caching.
 */
inline FlashCpp::TypeIndexArg makeTypeIndexArgFromSpec(const TypeSpecifierNode& spec) {
	FlashCpp::TypeIndexArg arg;
	arg.type_index = spec.type_index();
	arg.base_type = spec.type();  // Include base type for primitive types
	arg.cv_qualifier = spec.cv_qualifier();
	arg.ref_qualifier = spec.reference_qualifier();
	arg.pointer_depth = static_cast<uint8_t>(std::min(spec.pointer_depth(), size_t(255)));
	// Include array info - critical for differentiating T[] from T[N] from T
	arg.is_array = spec.is_array();
	arg.array_size = spec.array_size();
	return arg;
}

/**
 * Create a FunctionSignatureKey from function name and argument types
 * 
 * This creates a TypeIndex-based key for function lookup caching.
 * The key can be used as a hash map key for O(1) function resolution cache lookups.
 */
inline FlashCpp::FunctionSignatureKey makeFunctionSignatureKey(
	StringHandle function_name,
	const std::vector<TypeSpecifierNode>& argument_types) {
	
	FlashCpp::FunctionSignatureKey key(function_name);
	key.param_types.reserve(argument_types.size());
	
	for (const auto& arg_type : argument_types) {
		key.param_types.push_back(makeTypeIndexArgFromSpec(arg_type));
	}
	
	return key;
}

/**
 * Global function resolution cache
 * 
 * Caches resolved function overloads keyed by function name + argument signature.
 * This avoids repeated overload resolution for the same function call patterns.
 * 
 * Key: FunctionSignatureKey (function name + TypeIndex-based parameter types)
 * Value: Pointer to the selected function declaration ASTNode (or nullptr if no match)
 */
inline std::unordered_map<FlashCpp::FunctionSignatureKey, OverloadResolutionResult, 
                          FlashCpp::FunctionSignatureKeyHash>& getFunctionResolutionCache() {
	static std::unordered_map<FlashCpp::FunctionSignatureKey, OverloadResolutionResult,
	                          FlashCpp::FunctionSignatureKeyHash> cache;
	return cache;
}

/**
 * Clear the function resolution cache
 * 
 * Should be called when starting a new compilation unit or when
 * the symbol table changes (e.g., after parsing new declarations).
 */
inline void clearFunctionResolutionCache() {
	getFunctionResolutionCache().clear();
}

/**
 * Resolve overload with caching
 * 
 * First checks the cache for a previous resolution. If not found,
 * performs full overload resolution and caches the result.
 * 
 * @param function_name The function name handle
 * @param overloads Vector of candidate function overloads
 * @param argument_types Vector of argument TypeSpecifierNodes
 * @return OverloadResolutionResult with selected overload or no_match/ambiguous
 */
inline OverloadResolutionResult resolve_overload_cached(
	StringHandle function_name,
	const std::vector<ASTNode>& overloads,
	const std::vector<TypeSpecifierNode>& argument_types)
{
	// Build signature key for cache lookup
	auto key = makeFunctionSignatureKey(function_name, argument_types);
	
	// Check cache first
	auto& cache = getFunctionResolutionCache();
	auto it = cache.find(key);
	if (it != cache.end()) {
		// Cache hit - return the cached result directly (preserves ambiguous/no_match/match states)
		return it->second;
	}
	
	// Cache miss - perform full overload resolution
	auto result = resolve_overload(overloads, argument_types);
	
	// Cache the result
	cache[key] = result;
	
	return result;
}

