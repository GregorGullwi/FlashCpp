#pragma once

#include "AstNodeTypes.h"
#include "SymbolTable.h"
#include "CompileContext.h"
#include "ChunkedString.h"
#include <vector>
#include <optional>

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
inline bool hasConversionOperator(TypeIndex source_type_index, Type target_type, TypeIndex target_type_index = 0) {
	// First, try to get struct name from gTypeInfo and search gSymbolTable
	// This is needed during parsing when gTypeInfo.member_functions is not yet populated
	if (source_type_index > 0 && source_type_index < gTypeInfo.size()) {
		const TypeInfo& source_type_info = gTypeInfo[source_type_index];
		std::string_view struct_name = StringTable::getStringView(source_type_info.name());
		
		// Build the target type name for the operator
		std::string_view target_type_name;
		if (target_type == Type::Struct && target_type_index > 0 && target_type_index < gTypeInfo.size()) {
			target_type_name = StringTable::getStringView(gTypeInfo[target_type_index].name());
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
				if (base_spec.type_index > 0 && base_spec.type_index < gTypeInfo.size()) {
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
	// Check reference compatibility
	if (from.is_reference() || to.is_reference()) {
		// If 'to' is a reference, 'from' must be compatible
		if (to.is_reference()) {
			// Check if both are references
			if (from.is_reference()) {
				// Both are references - check reference kind
				bool from_is_rvalue = from.is_rvalue_reference();
				bool to_is_rvalue = to.is_rvalue_reference();
				
				// Exact match: both lvalue ref or both rvalue ref, same base type
				if (from_is_rvalue == to_is_rvalue && from.type() == to.type()) {
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
				
				// Check if base types are compatible
				bool types_match = (from.type() == to.type());
				if (!types_match) {
					// Allow conversions for const lvalue refs only
					auto conversion = can_convert_type(from.type(), to.type());
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
			if (from.type() == to.type()) {
				return TypeConversionResult::exact_match();
			}
			return can_convert_type(from.type(), to.type());
		}
	}
	
	// Check pointer compatibility
	if (from.is_pointer() || to.is_pointer()) {
		// Both must be pointers for conversion
		if (from.is_pointer() != to.is_pointer()) {
			// Special case: nullptr (represented as 0) can convert to any pointer
			// But we don't have a way to detect that here yet
			return TypeConversionResult::no_match();
		}

		// Pointer depth must match
		if (from.pointer_depth() != to.pointer_depth()) {
			return TypeConversionResult::no_match();
		}

		// For now, require exact type match for pointers
		// TODO: Handle pointer conversions (derived to base, void*, etc.)
		if (from.type() == to.type()) {
			return TypeConversionResult::exact_match();
		}

		return TypeConversionResult::no_match();
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

	// Non-pointer, non-reference types: use basic type conversion
	return can_convert_type(from.type(), to.type());
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
		// For non-variadic functions, argument count must match exactly
		if (is_variadic) {
			if (argument_types.size() < parameters.size()) {
				continue;  // Too few arguments for variadic function
			}
		} else {
			if (parameters.size() != argument_types.size()) {
				continue;  // Argument count mismatch for non-variadic function
			}
		}
		
		// Check if all arguments can be converted to parameters
		// For variadic functions, only check the named parameters
		// The variadic arguments (...) accept any type
		std::vector<ConversionRank> conversion_ranks;
		bool all_convertible = true;

		size_t params_to_check = parameters.size();  // For variadic, only check named params

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
			} else if (!this_is_better && !this_is_worse) {
				// This overload is equally good - ambiguous
				num_best_matches++;
			}
			// If this_is_worse, ignore this overload
		}
	}
	
	if (best_match == nullptr) {
		return OverloadResolutionResult::no_match();
	}
	
	if (num_best_matches > 1) {
		return OverloadResolutionResult::ambiguous();
	}
	
	return OverloadResolutionResult(best_match);
}

// Result of operator overload resolution
struct OperatorOverloadResult {
	const StructMemberFunction* member_overload = nullptr;
	bool has_overload = false;
	
	OperatorOverloadResult() = default;
	OperatorOverloadResult(const StructMemberFunction* overload) 
		: member_overload(overload), has_overload(true) {}
	
	static OperatorOverloadResult no_overload() {
		return OperatorOverloadResult();
	}
};

// Find operator overload in a struct type
// Returns the member function that overloads the given operator, or nullptr if not found
inline OperatorOverloadResult findUnaryOperatorOverload(TypeIndex operand_type_index, std::string_view operator_symbol) {
	// Only struct types can have operator overloads
	if (operand_type_index == 0 || operand_type_index >= gTypeInfo.size()) {
		return OperatorOverloadResult::no_overload();
	}
	
	const TypeInfo& type_info = gTypeInfo[operand_type_index];
	const StructTypeInfo* struct_info = type_info.getStructInfo();
	
	if (!struct_info) {
		return OperatorOverloadResult::no_overload();
	}
	
	// Search for the operator overload in member functions
	for (const auto& member_func : struct_info->member_functions) {
		if (member_func.is_operator_overload && member_func.operator_symbol == operator_symbol) {
			return OperatorOverloadResult(&member_func);
		}
	}
	
	// Search base classes recursively
	for (const auto& base_spec : struct_info->base_classes) {
		if (base_spec.type_index > 0 && base_spec.type_index < gTypeInfo.size()) {
			auto result = findUnaryOperatorOverload(base_spec.type_index, operator_symbol);
			if (result.has_overload) {
				return result;
			}
		}
	}
	
	return OperatorOverloadResult::no_overload();
}


