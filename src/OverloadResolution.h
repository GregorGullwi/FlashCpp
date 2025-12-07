#pragma once

#include "AstNodeTypes.h"
#include "SymbolTable.h"
#include <vector>
#include <optional>

// Conversion rank for overload resolution
// Lower rank = better match
enum class ConversionRank {
	ExactMatch = 0,          // No conversion needed
	Promotion = 1,           // Integral or floating-point promotion
	Conversion = 2,          // Standard conversion (int to double, etc.)
	UserDefined = 3,         // User-defined conversion (not implemented yet)
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
	
	// No valid conversion
	return TypeConversionResult::no_match();
}

// Check if one type can be implicitly converted to another (considering pointers and references)
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
				// Can't bind non-reference to reference parameter
				// (This would require creating a temporary, which isn't allowed for lvalue refs)
				return TypeConversionResult::no_match();
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

