#include "Parser.h"
#include "ConstExprEvaluator.h"
#include "BuiltinListInitNarrowing.h"
#include "CallNodeHelpers.h"
#include "ExpressionSubstitutor.h"
#include "MemberFunctionLookupShared.h"
#include "OverloadResolution.h"
#include "SemanticAnalysis.h"
#include "TypeTraitEvaluator.h"
#include <algorithm>
#include <limits>

#include "ConstExprEvalHelpers.h"

namespace ConstExpr {
// ============================================================================
// evaluate_initializer_list_construction
//
// Materialises a std::initializer_list<T> value during constant evaluation.
//
// The standard (C++20 [dcl.init.list] / [stmt.ranged]) requires the compiler to
// create a backing array and construct the initializer_list around it.  We
// synthesise this at the constexpr level as follows:
//
//   1. Evaluate all element expressions.
//   2. Allocate a synthetic backing-array binding keyed "@ilist_N".
//   3. Build begin_ptr (offset=0) and end_ptr (offset=N), both carrying a full
//      pointer_value_snapshot so that pointer arithmetic and deref work even
//      after the binding goes out of scope.
//   4. Try to use the initializer_list constructor with the pre-evaluated pointer
//      args.  Fall back to direct aggregate assignment otherwise.
// ============================================================================
EvalResult Evaluator::evaluate_initializer_list_construction(
	const InitializerListConstructionNode& ilist_node,
	const std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	std::unordered_map<std::string_view, EvalResult>* mutable_bindings) {

	// Step 1: materialize the backing array using the declared initializer_list
	// element type so aggregate/class elements keep their full object bindings.
	const ASTNode& element_type_node = ilist_node.element_type();
	if (!element_type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("InitializerListConstruction: element_type is not TypeSpecifierNode");
	}
	const TypeSpecifierNode& element_type_spec = element_type_node.as<TypeSpecifierNode>();
	InitializerListNode backing_init_list;
	for (const ASTNode& elem_node : ilist_node.elements()) {
		backing_init_list.add_initializer(elem_node);
	}
	EvalResult materialized_array = materialize_array_value(
		element_type_spec.type_index(),
		backing_init_list,
		context,
		&bindings);
	if (!materialized_array.success()) {
		return materialized_array;
	}
	EvalResult backing_array = std::move(materialized_array);
	if (backing_array.array_elements.empty() && !backing_array.array_values.empty()) {
		// materialize_array_value keeps legacy scalar arrays in array_values only;
		// initializer_list pointer snapshots always need element-shaped entries.
		backing_array.array_elements.reserve(backing_array.array_values.size());
		for (int64_t value : backing_array.array_values) {
			backing_array.array_elements.push_back(EvalResult::from_int(value));
		}
	}
	const size_t n = backing_array.array_elements.size();

	// Step 2: allocate a synthetic backing-array key using a dedicated counter so that
	// @ilist_N names never share the same numeric space as @new_N heap allocations.
	// The key is not registered in constexpr_heap (it is not a `new` allocation and
	// must not be freed by `delete`).
	StringHandle backing_handle = context.alloc_ilist_slot();
	// getStringView() returns a view into the interned string table which outlives
	// the evaluation, so this view is a stable key for the local binding map.
	std::string_view backing_key = StringTable::getStringView(backing_handle);

	// Store backing array in mutable_bindings so that deref_pointer_with_bindings can
	// find it by name during pointer dereference operations.
	if (mutable_bindings) {
		(*mutable_bindings)[backing_key] = backing_array;
	} else if (context.local_bindings) {
		(*context.local_bindings)[backing_key] = backing_array;
	}

	// Step 3: build begin/end pointers with full snapshot so pointer arithmetic and
	// deref work even in a different scope.
	EvalResult begin_ptr = EvalResult::from_pointer(backing_handle, 0);
	begin_ptr.pointer_value_snapshot = backing_array.array_elements;

	EvalResult end_ptr = EvalResult::from_pointer(backing_handle, static_cast<int64_t>(n));
	end_ptr.pointer_value_snapshot = backing_array.array_elements;

	EvalResult int_size = EvalResult::from_uint(static_cast<unsigned long long>(n));

	// Step 4: resolve the initializer_list struct type.
	const ASTNode& target_type_node = ilist_node.target_type();
	if (!target_type_node.is<TypeSpecifierNode>())
		return EvalResult::error("InitializerListConstruction: target_type is not TypeSpecifierNode");
	const TypeSpecifierNode& target_type_spec = target_type_node.as<TypeSpecifierNode>();

	const StructTypeInfo* struct_info = get_struct_info_from_type(target_type_spec);
	if (!struct_info)
		return EvalResult::error("InitializerListConstruction: target type is not a struct");

	TypeIndex type_index = target_type_spec.type_index();

	// Determine whether the second member is a pointer (last_) or a size value (size_).
	// This mirrors the runtime IR generator in generateInitializerListConstructionIr.
	bool second_member_is_pointer = false;
	if (struct_info->members.size() >= 2) {
		second_member_is_pointer = struct_info->members[1].pointer_depth > 0;
	}

	// Choose the pre-evaluated args depending on the layout.
	std::vector<EvalResult> ctor_args_vec = second_member_is_pointer
		? std::vector<EvalResult>{begin_ptr, end_ptr}
		: std::vector<EvalResult>{begin_ptr, int_size};

	// Step 5: build the output EvalResult shell.
	EvalResult result = EvalResult::from_int(0LL);
	result.object_type_index = type_index;

	// Step 6: try constructor approach.
	// Look for any 2-parameter constructor (skip implicit copy/move constructors).
	if (struct_info->hasUserDeclaredConstructor()) {
		auto ctor_candidates = struct_info->getConstructorsByParameterCount(2, true);
		const ConstructorDeclarationNode* matching_ctor = nullptr;
		for (const StructMemberFunction* candidate : ctor_candidates) {
			if (candidate && candidate->function_decl.is<ConstructorDeclarationNode>()) {
				matching_ctor = &candidate->function_decl.as<ConstructorDeclarationNode>();
				break;
			}
		}

		if (matching_ctor) {
			std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
			// Also inject the backing array so that deref works inside the constructor body.
			ctor_param_bindings[backing_key] = backing_array;

			auto bind_result = bind_pre_evaluated_arguments(
				matching_ctor->parameter_nodes(),
				ctor_args_vec,
				ctor_param_bindings,
				context,
				"InitializerListConstruction: failed to bind constructor parameters",
				false);
			if (bind_result.success()) {
				auto materialize_result = materialize_members_from_constructor(
					struct_info,
					*matching_ctor,
					ctor_param_bindings,
					result.object_member_bindings,
					context,
					true);  // ignore_default_initializer_errors
				if (materialize_result.success()) {
					// Ensure synthesized iterator pointers keep their snapshot payload even
					// when constructor materialization routes through parameter/member
					// rewrites that may drop pointer_value_snapshot metadata.
					if (struct_info->members.size() > kInitializerListBeginMemberIndex) {
						std::string_view m0 =
							StringTable::getStringView(struct_info->members[kInitializerListBeginMemberIndex].getName());
						result.object_member_bindings[m0] = begin_ptr;
					}
					if (struct_info->members.size() > kInitializerListEndOrSizeMemberIndex) {
						std::string_view m1 =
							StringTable::getStringView(struct_info->members[kInitializerListEndOrSizeMemberIndex].getName());
						result.object_member_bindings[m1] = second_member_is_pointer ? end_ptr : int_size;
					}
					return result;
				}
			}
		}
	}

	// Step 7: fallback — assign members directly based on their position and pointer_depth.
	// member[kInitializerListBeginMemberIndex] always receives begin_ptr (first_ / data_).
	// member[kInitializerListEndOrSizeMemberIndex] receives end_ptr if it is a pointer (last_),
	// or int_size (size_) otherwise.
	if (struct_info->members.size() > kInitializerListBeginMemberIndex) {
		std::string_view m0 =
			StringTable::getStringView(struct_info->members[kInitializerListBeginMemberIndex].getName());
		result.object_member_bindings[m0] = begin_ptr;
	}
	if (struct_info->members.size() > kInitializerListEndOrSizeMemberIndex) {
		std::string_view m1 =
			StringTable::getStringView(struct_info->members[kInitializerListEndOrSizeMemberIndex].getName());
		result.object_member_bindings[m1] = second_member_is_pointer ? end_ptr : int_size;
	}

	return result;
}

// Helper functions for overflow-safe arithmetic using compiler builtins
// Perform addition with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_add(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
		return std::nullopt; // Overflow
	}
	result = a + b;
	bool overflow = false;
#else
	bool overflow = __builtin_add_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform subtraction with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_sub(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
		return std::nullopt; // Overflow
	}
	result = a - b;
	bool overflow = false;
#else
	bool overflow = __builtin_sub_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform multiplication with overflow checking, return result or nullopt on overflow
std::optional<long long> Evaluator::safe_mul(long long a, long long b) {
	long long result;
#if defined(_MSC_VER) && !defined(__clang__)
	// MSVC implementation using manual overflow detection
	if (a == 0 || b == 0) {
		result = 0;
	} else if (a == LLONG_MIN || b == LLONG_MIN) {
		// Special case: LLONG_MIN * anything except 0 or 1 overflows
		if ((a == LLONG_MIN && (b < -1 || b > 1)) || (b == LLONG_MIN && (a < -1 || a > 1))) {
			return std::nullopt;
		}
		result = a * b;
	} else if ((a > 0 && b > 0 && a > LLONG_MAX / b) ||
			   (a > 0 && b < 0 && b < LLONG_MIN / a) ||
			   (a < 0 && b > 0 && a < LLONG_MIN / b) ||
			   (a < 0 && b < 0 && a < LLONG_MAX / b)) {
		return std::nullopt; // Overflow
	} else {
		result = a * b;
	}
	bool overflow = false;
#else
	bool overflow = __builtin_mul_overflow(a, b, &result);
#endif
	return overflow ? std::nullopt : std::optional<long long>(result);
}

// Perform left shift with validation and overflow checking, return result or nullopt on error
std::optional<long long> Evaluator::safe_shl(long long a, long long b, int width_bits) {
	width_bits = normalize_shift_width(width_bits);
	if (b < 0 || b >= width_bits) {
		return std::nullopt; // Negative shift or shift >= bit width is undefined
	}
	if (a == 0) {
		return 0; // Shifting zero is fine
	}

	// Check if the shift would cause bits to be lost
	// For left shift, check if any bits would be shifted out
	long long shifted = a << b;
	long long back_shifted = shifted >> b;
	if (back_shifted != a) {
		return std::nullopt; // Overflow detected
	}

	return shifted;
}

// Perform right shift with validation, return result or nullopt on error
std::optional<long long> Evaluator::safe_shr(long long a, long long b, int width_bits) {
	width_bits = normalize_shift_width(width_bits);
	if (b < 0 || b >= width_bits) {
		return std::nullopt; // Negative shift or shift >= bit width is undefined
	}
	return a >> b; // Right shift never overflows mathematically
}

// Helper to apply binary operators
EvalResult Evaluator::apply_binary_op(
	const EvalResult& lhs, const EvalResult& rhs, std::string_view op,
	EvaluationContext* context,
	const std::unordered_map<std::string_view, EvalResult>* bindings) {
	if (lhs.is_member_pointer() || rhs.is_member_pointer()) {
		const bool lhs_is_member_ptr = lhs.is_member_pointer();
		const bool rhs_is_member_ptr = rhs.is_member_pointer();
		auto extractIntegerValueAsUnsigned = [](const EvalResult& value) -> unsigned long long {
			return value.is_uint() ? value.as_uint_raw()
								   : static_cast<unsigned long long>(value.as_int());
		};

		if (op == "==" || op == "!=") {
			bool are_equal = false;
			if (lhs_is_member_ptr && rhs_is_member_ptr) {
				are_equal =
					(lhs.is_null_member_pointer == rhs.is_null_member_pointer) &&
					(lhs.member_pointer_member == rhs.member_pointer_member) &&
					(lhs.as_int() == rhs.as_int());
			} else if (lhs_is_member_ptr) {
				const unsigned long long rhs_raw = extractIntegerValueAsUnsigned(rhs);
				if (rhs_raw != 0ULL) {
					return EvalResult::error("Member pointer comparison with non-zero integer is not supported in constant expressions");
				}
				are_equal = lhs.is_null_member_pointer;
			} else {
				const unsigned long long lhs_raw = extractIntegerValueAsUnsigned(lhs);
				if (lhs_raw != 0ULL) {
					return EvalResult::error("Member pointer comparison with non-zero integer is not supported in constant expressions");
				}
				are_equal = rhs.is_null_member_pointer;
			}
			return EvalResult::from_bool(op == "==" ? are_equal : !are_equal);
		}

		return EvalResult::error("Operator '" + std::string(op) + "' on member pointer value is not supported in constant expressions");
	}

	// Handle operations involving constexpr pointers.
	// Valid constexpr pointers (pointer_to_var.isValid()) are always non-null since they
	// represent address-of named constexpr variables.  nullptr evaluates to integer 0.
	if (lhs.pointer_to_var.isValid() || rhs.pointer_to_var.isValid()) {
		const bool lhs_is_ptr = lhs.pointer_to_var.isValid();
		const bool rhs_is_ptr = rhs.pointer_to_var.isValid();

		// Equality / inequality comparisons
		if (op == "==" || op == "!=") {
			bool are_equal;
			if (lhs_is_ptr && rhs_is_ptr) {
				// ptr1 == ptr2: equal iff they point to the same named variable AND offset
				are_equal = (lhs.pointer_to_var == rhs.pointer_to_var) && (lhs.pointer_offset == rhs.pointer_offset);
			} else if (lhs_is_ptr) {
				// ptr == integer: nullptr (0) is the only null-pointer constant.
				// Use raw unsigned comparison to avoid implementation-defined signed overflow.
				const unsigned long long rhs_raw = rhs.is_uint() ? rhs.as_uint_raw()
																 : static_cast<unsigned long long>(rhs.as_int());
				if (rhs_raw != 0ULL) {
					return EvalResult::error("Pointer comparison with non-zero integer is not supported in constant expressions");
				}
				are_equal = false; // valid constexpr pointer is always non-null
			} else {
				// integer == ptr: same treatment as above
				const unsigned long long lhs_raw = lhs.is_uint() ? lhs.as_uint_raw()
																 : static_cast<unsigned long long>(lhs.as_int());
				if (lhs_raw != 0ULL) {
					return EvalResult::error("Pointer comparison with non-zero integer is not supported in constant expressions");
				}
				are_equal = false; // valid constexpr pointer is always non-null
			}
			return EvalResult::from_bool(op == "==" ? are_equal : !are_equal);
		}

		// Relational comparisons on pointers into the same array
		if (op == "<" || op == "<=" || op == ">" || op == ">=") {
			if (lhs_is_ptr && rhs_is_ptr) {
				if (lhs.pointer_to_var != rhs.pointer_to_var) {
					return EvalResult::error("Relational comparison between pointers to different variables is not allowed in constant expressions", EvalErrorType::NotConstantExpression);
				}
				if (op == "<")
					return EvalResult::from_bool(lhs.pointer_offset < rhs.pointer_offset);
				if (op == "<=")
					return EvalResult::from_bool(lhs.pointer_offset <= rhs.pointer_offset);
				if (op == ">")
					return EvalResult::from_bool(lhs.pointer_offset > rhs.pointer_offset);
				return EvalResult::from_bool(lhs.pointer_offset >= rhs.pointer_offset);
			}
			return EvalResult::error("Relational comparison between pointer and integer is not supported in constant expressions");
		}

		// Pointer arithmetic: ptr + n, n + ptr, ptr - n, ptr - ptr
		if (op == "+") {
			if (lhs_is_ptr && !rhs_is_ptr) {
				// ptr + n
				const auto new_offset = safe_add(lhs.pointer_offset, rhs.as_int());
				if (!new_offset.has_value()) {
					return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
				}
				auto result = make_checked_constexpr_pointer_result(
					StringTable::getStringView(lhs.pointer_to_var),
					*new_offset,
					context,
					bindings);
				if (result.success() && !lhs.pointer_value_snapshot.empty()) {
					result.pointer_value_snapshot = lhs.pointer_value_snapshot;
				}
				return result;
			}
			if (!lhs_is_ptr && rhs_is_ptr) {
				// n + ptr
				const auto new_offset = safe_add(rhs.pointer_offset, lhs.as_int());
				if (!new_offset.has_value()) {
					return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
				}
				auto result = make_checked_constexpr_pointer_result(
					StringTable::getStringView(rhs.pointer_to_var),
					*new_offset,
					context,
					bindings);
				if (result.success() && !rhs.pointer_value_snapshot.empty()) {
					result.pointer_value_snapshot = rhs.pointer_value_snapshot;
				}
				return result;
			}
			return EvalResult::error("Addition of two pointers is not allowed in constant expressions", EvalErrorType::NotConstantExpression);
		}
		if (op == "-") {
			if (lhs_is_ptr && !rhs_is_ptr) {
				// ptr - n
				const auto new_offset = safe_sub(lhs.pointer_offset, rhs.as_int());
				if (!new_offset.has_value()) {
					return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
				}
				auto result = make_checked_constexpr_pointer_result(
					StringTable::getStringView(lhs.pointer_to_var),
					*new_offset,
					context,
					bindings);
				if (result.success() && !lhs.pointer_value_snapshot.empty()) {
					result.pointer_value_snapshot = lhs.pointer_value_snapshot;
				}
				return result;
			}
			if (lhs_is_ptr && rhs_is_ptr) {
				// ptr - ptr: both must point into the same array
				if (lhs.pointer_to_var != rhs.pointer_to_var) {
					return EvalResult::error("Subtraction of pointers to different variables is not allowed in constant expressions", EvalErrorType::NotConstantExpression);
				}
				return EvalResult::from_int(lhs.pointer_offset - rhs.pointer_offset);
			}
			return EvalResult::error("Subtraction of pointer from integer is not allowed in constant expressions");
		}

		// Logical operators: a valid constexpr pointer is always truthy
		if (op == "&&") {
			const bool lhs_bool = lhs_is_ptr ? true : lhs.as_bool();
			const bool rhs_bool = rhs_is_ptr ? true : rhs.as_bool();
			return EvalResult::from_bool(lhs_bool && rhs_bool);
		}
		if (op == "||") {
			const bool lhs_bool = lhs_is_ptr ? true : lhs.as_bool();
			const bool rhs_bool = rhs_is_ptr ? true : rhs.as_bool();
			return EvalResult::from_bool(lhs_bool || rhs_bool);
		}

		// All other pointer operations are unsupported
		return EvalResult::error("Unsupported pointer operation '" + std::string(op) + "' in constant expressions");
	}

	// Array decay: `array + n` or `n + array` produces a pointer to element n of the array.
	// This enables `return data + size;` style begin()/end() member functions in constexpr
	// range-based for loops, where `data` is a member array that has been tagged with its
	// origin variable name via array_origin_var.
	if (lhs.is_array && lhs.array_origin_var.isValid() && !rhs.is_array && !rhs.pointer_to_var.isValid() && op == "+") {
		int64_t offset = rhs.as_int();
		return make_checked_constexpr_pointer_result(
			StringTable::getStringView(lhs.array_origin_var),
			offset,
			context,
			bindings);
	}
	if (rhs.is_array && rhs.array_origin_var.isValid() && !lhs.is_array && !lhs.pointer_to_var.isValid() && op == "+") {
		int64_t offset = lhs.as_int();
		return make_checked_constexpr_pointer_result(
			StringTable::getStringView(rhs.array_origin_var),
			offset,
			context,
			bindings);
	}

	// Determine the operand kinds so we can dispatch to the correct domain.
	// This mirrors C++ "usual arithmetic conversions" (C++20 [expr.arith.conv]):
	//   1. If either operand is floating-point → promote both to double.
	//   2. Else if either operand is unsigned long long → promote both to
	//      unsigned long long (the signed value is reinterpreted as unsigned,
	//      matching the standard's rule that the signed operand converts to the
	//      unsigned type when the unsigned type is at least as wide).
	//   3. Otherwise → use signed long long arithmetic.
	const bool lhs_is_double = std::holds_alternative<double>(lhs.value);
	const bool rhs_is_double = std::holds_alternative<double>(rhs.value);
	const bool either_is_double = lhs_is_double || rhs_is_double;

	const bool lhs_is_uint = lhs.is_uint();
	const bool rhs_is_uint = rhs.is_uint();
	const bool either_is_uint = lhs_is_uint || rhs_is_uint;

	// --- Floating-point path ---
	// If either operand is a floating-point double (or float, which is stored as
	// double), use floating-point arithmetic/comparison for all operations.
	if (either_is_double) {
		const double lv = lhs.as_double();
		const double rv = rhs.as_double();

		if (op == "+")
			return EvalResult::from_double(lv + rv);
		if (op == "-")
			return EvalResult::from_double(lv - rv);
		if (op == "*")
			return EvalResult::from_double(lv * rv);
		if (op == "/") {
			if (rv == 0.0)
				return EvalResult::error("Division by zero in constant expression");
			return EvalResult::from_double(lv / rv);
		}
		// Bitwise operators are not valid on floating-point types.
		if (op == "==")
			return EvalResult::from_bool(lv == rv);
		if (op == "!=")
			return EvalResult::from_bool(lv != rv);
		if (op == "<")
			return EvalResult::from_bool(lv < rv);
		if (op == "<=")
			return EvalResult::from_bool(lv <= rv);
		if (op == ">")
			return EvalResult::from_bool(lv > rv);
		if (op == ">=")
			return EvalResult::from_bool(lv >= rv);
		if (op == "&&")
			return EvalResult::from_bool(lv != 0.0 && rv != 0.0);
		if (op == "||")
			return EvalResult::from_bool(lv != 0.0 || rv != 0.0);
		return EvalResult::error("Operator '" + std::string(op) + "' not supported for floating-point constants");
	}

	// --- Unsigned integer path ---
	// When either operand is stored as unsigned long long, use unsigned
	// arithmetic and comparison.  The other operand is converted to
	// unsigned long long via static_cast (reinterpreting negative signed
	// values as large unsigned values), matching C++ usual arithmetic
	// conversions when the unsigned type is at least as wide as the signed type.
	if (either_is_uint) {
		// Read each side preserving its full bit pattern using as_uint_raw()
		// to avoid the signed round-trip in as_int() for values above LLONG_MAX.
		const unsigned long long lv = lhs.as_uint_raw();
		const unsigned long long rv = rhs.as_uint_raw();

		// Propagate the exact result type (usual arithmetic conversions) so that
		// (a) subsequent shift-count validation uses the correct operand width,
		// and (b) unsigned arithmetic wraps at the declared type's width rather
		// than at the evaluator's internal 64-bit storage width.
		const auto result_type = get_binary_arithmetic_result_type(lhs, rhs);

		const auto make_arith = [&](unsigned long long val) -> EvalResult {
			EvalResult result = EvalResult::from_uint(apply_uint_type_mask(val, result_type));
			if (result_type) {
				result.set_exact_type(*result_type);
			}
			return result;
		};

		if (op == "+")
			return make_arith(lv + rv);
		if (op == "-")
			return make_arith(lv - rv);
		if (op == "*")
			return make_arith(lv * rv);
		if (op == "/") {
			if (rv == 0)
				return EvalResult::error("Division by zero in constant expression", EvalErrorType::NotConstantExpression);
			return make_arith(lv / rv);
		}
		if (op == "%") {
			if (rv == 0)
				return EvalResult::error("Modulo by zero in constant expression", EvalErrorType::NotConstantExpression);
			return make_arith(lv % rv);
		}
		if (op == "&")
			return make_arith(lv & rv);
		if (op == "|")
			return make_arith(lv | rv);
		if (op == "^")
			return make_arith(lv ^ rv);
		if (op == "<<") {
			const ShiftEvaluationInfo shift_info = get_shift_evaluation_info(lhs);
			if (rv >= static_cast<unsigned long long>(shift_info.width_bits)) {
				return EvalResult::error("Left shift count >= width of type in constant expression", EvalErrorType::NotConstantExpression);
			}
			return make_shift_result(shift_info.promoted_type, apply_uint_type_mask(lv << rv, shift_info.promoted_type));
		}
		if (op == ">>") {
			const ShiftEvaluationInfo shift_info = get_shift_evaluation_info(lhs);
			if (rv >= static_cast<unsigned long long>(shift_info.width_bits)) {
				return EvalResult::error("Right shift count >= width of type in constant expression", EvalErrorType::NotConstantExpression);
			}
			return make_shift_result(shift_info.promoted_type, apply_uint_type_mask(lv >> rv, shift_info.promoted_type));
		}
		if (op == "==")
			return EvalResult::from_bool(lv == rv);
		if (op == "!=")
			return EvalResult::from_bool(lv != rv);
		if (op == "<=>")
			return EvalResult::from_int(lv < rv ? -1 : (lv == rv ? 0 : 1));
		if (op == "<")
			return EvalResult::from_bool(lv < rv);
		if (op == "<=")
			return EvalResult::from_bool(lv <= rv);
		if (op == ">")
			return EvalResult::from_bool(lv > rv);
		if (op == ">=")
			return EvalResult::from_bool(lv >= rv);
		if (op == "&&")
			return EvalResult::from_bool(lv != 0 && rv != 0);
		if (op == "||")
			return EvalResult::from_bool(lv != 0 || rv != 0);
		return EvalResult::error("Operator '" + std::string(op) + "' not supported for unsigned constants");
	}

	// --- Signed integer path (default) ---
	long long lhs_val = lhs.as_int();
	long long rhs_val = rhs.as_int();

	// Propagate the exact result type so that subsequent shift-count validation
	// uses the correct operand width (e.g. (a + b) << 33 is rejected when a, b
	// are int).
	const auto signed_result_type = get_binary_arithmetic_result_type(lhs, rhs);

	const auto make_signed = [&](long long val) -> EvalResult {
		EvalResult result = EvalResult::from_int(val);
		if (signed_result_type) {
			result.set_exact_type(*signed_result_type);
		}
		return result;
	};

	// Handle arithmetic operators with overflow checking
	if (op == "+") {
		if (auto result = safe_add(lhs_val, rhs_val)) {
			return make_signed(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
		}
	} else if (op == "-") {
		if (auto result = safe_sub(lhs_val, rhs_val)) {
			return make_signed(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
		}
	} else if (op == "*") {
		if (auto result = safe_mul(lhs_val, rhs_val)) {
			return make_signed(*result);
		} else {
			return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
		}
	} else if (op == "/") {
		if (rhs_val == 0) {
			return EvalResult::error("Division by zero in constant expression", EvalErrorType::NotConstantExpression);
		}
		// Check for overflow in division (only happens with LLONG_MIN / -1)
		if (lhs_val == LLONG_MIN && rhs_val == -1) {
			return EvalResult::error("Signed integer overflow in constant expression", EvalErrorType::NotConstantExpression);
		}
		return make_signed(lhs_val / rhs_val);
	} else if (op == "%") {
		if (rhs_val == 0) {
			return EvalResult::error("Modulo by zero in constant expression", EvalErrorType::NotConstantExpression);
		}
		return make_signed(lhs_val % rhs_val);
	}

	// Handle bitwise operators
	else if (op == "&") {
		return make_signed(lhs_val & rhs_val);
	} else if (op == "|") {
		return make_signed(lhs_val | rhs_val);
	} else if (op == "^") {
		return make_signed(lhs_val ^ rhs_val);
	} else if (op == "<<") {
		const ShiftEvaluationInfo shift_info = get_shift_evaluation_info(lhs);
		if (auto result = safe_shl(lhs_val, rhs_val, shift_info.width_bits)) {
			return make_shift_result(shift_info.promoted_type, *result);
		} else {
			return EvalResult::error("Left shift overflow or invalid shift count in constant expression", EvalErrorType::NotConstantExpression);
		}
	} else if (op == ">>") {
		const ShiftEvaluationInfo shift_info = get_shift_evaluation_info(lhs);
		if (auto result = safe_shr(lhs_val, rhs_val, shift_info.width_bits)) {
			return make_shift_result(shift_info.promoted_type, *result);
		} else {
			return EvalResult::error("Invalid shift count in constant expression", EvalErrorType::NotConstantExpression);
		}
	}

	// Handle comparison operators for signed integers
	if (op == "==") {
		return EvalResult::from_bool(lhs_val == rhs_val);
	} else if (op == "!=") {
		return EvalResult::from_bool(lhs_val != rhs_val);
	} else if (op == "<=>") {
		return EvalResult::from_int(lhs_val < rhs_val ? -1 : (lhs_val == rhs_val ? 0 : 1));
	} else if (op == "<") {
		return EvalResult::from_bool(lhs_val < rhs_val);
	} else if (op == "<=") {
		return EvalResult::from_bool(lhs_val <= rhs_val);
	} else if (op == ">") {
		return EvalResult::from_bool(lhs_val > rhs_val);
	} else if (op == ">=") {
		return EvalResult::from_bool(lhs_val >= rhs_val);
	} else if (op == "&&") {
		return EvalResult::from_bool(lhs.as_bool() && rhs.as_bool());
	} else if (op == "||") {
		return EvalResult::from_bool(lhs.as_bool() || rhs.as_bool());
	} else if (op == ",") {
		return rhs;
	}

	// Unsupported operator
	return EvalResult::error("Operator '" + std::string(op) + "' not supported in constant expressions");
}

EvalResult Evaluator::apply_unary_op(const EvalResult& operand, std::string_view op) {
	// Handle unary operators on constexpr pointers.
	// A valid constexpr pointer (pointer_to_var.isValid()) is always non-null (truthy).
	// Address-of (&) and dereference (*) are handled before apply_unary_op is called.
	if (operand.pointer_to_var.isValid()) {
		if (op == "!") {
			return EvalResult::from_bool(false); // !non_null_ptr is false
		}
		return EvalResult::error("Unary operator '" + std::string(op) + "' on pointer value is not supported in constant expressions");
	}
	if (operand.is_member_pointer()) {
		if (op == "!") {
			return EvalResult::from_bool(operand.is_null_member_pointer);
		}
		return EvalResult::error("Unary operator '" + std::string(op) + "' on member pointer value is not supported in constant expressions");
	}

	const bool operand_is_uint = operand.is_uint();

	// Helper: build an unsigned result masked to the operand's declared type width.
	const auto make_uint = [&](unsigned long long raw) {
		EvalResult result = EvalResult::from_uint(apply_uint_type_mask(raw, operand.exact_type));
		if (operand.exact_type.has_value()) {
			result.set_exact_type(*operand.exact_type);
		}
		return result;
	};

	// Helper: build a signed result and propagate exact_type.
	const auto make_sint = [&](long long val) {
		EvalResult result = EvalResult::from_int(val);
		if (operand.exact_type.has_value()) {
			result.set_exact_type(*operand.exact_type);
		}
		return result;
	};

	if (op == "!") {
		return EvalResult::from_bool(!operand.as_bool());
	} else if (op == "~") {
		if (operand_is_uint) {
			return make_uint(~operand.as_uint_raw());
		}
		return make_sint(~operand.as_int());
	} else if (op == "-") {
		// Unary minus - negate the value
		if (std::holds_alternative<double>(operand.value)) {
			return EvalResult::from_double(-operand.as_double());
		}
		if (operand_is_uint) {
			// Unary minus on unsigned: wraps at declared type width (e.g. -(1u) == UINT_MAX)
			return make_uint(static_cast<unsigned long long>(0) - operand.as_uint_raw());
		}
		// Check for overflow: negating LLONG_MIN overflows
		const long long val = operand.as_int();
		if (val == LLONG_MIN) {
			return EvalResult::error("Signed integer overflow in unary minus", EvalErrorType::NotConstantExpression);
		}
		return make_sint(-val);
	} else if (op == "+") {
		// Unary plus - no-op, just return the value
		return operand;
	}

	// Unsupported operator
	return EvalResult::error("Unary operator '" + std::string(op) + "' not supported in constant expressions");
}

} // namespace ConstExpr
