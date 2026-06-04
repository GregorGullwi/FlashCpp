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
#include <numeric>

#include "ConstExprEvalHelpers.h"

namespace ConstExpr {

// Shared helper: bind struct members from an InitializerListNode (aggregate init)
// and apply default member initializers for any members not covered by the list.
EvalResult Evaluator::materialize_aggregate_object_value(
	const StructTypeInfo* struct_info,
	TypeIndex type_index,
	const InitializerListNode& init_list,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings) {
	if (!struct_info) {
		return EvalResult::error("Aggregate object is not a struct");
	}
	if (struct_info->hasUserDeclaredConstructor()) {
		return EvalResult::error(std::string(StringBuilder()
			.append("Type '"sv)
			.append(StringTable::getStringView(struct_info->getName()))
			.append("' has user-declared constructors and is not an aggregate"sv)
			.commit()), EvalErrorType::NotConstantExpression);
	}

	EvalResult object_result = EvalResult::from_int(0);
	object_result.object_type_index = type_index;
	auto bind_members_result = bind_members_from_initializer_list(struct_info, init_list, object_result.object_member_bindings, context, outer_bindings);
	if (!bind_members_result.success()) {
		return bind_members_result;
	}
	return object_result;
}

EvalResult Evaluator::materialize_constructor_object_value(
	const ConstructorCallNode& ctor_call,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings) {
	const TypeSpecifierNode& type_spec = ctor_call.type_node();
	if (!is_struct_type(type_spec.category())) {
		return EvalResult::error("Constructor call is not a struct/class type");
	}

	TypeIndex type_index = type_spec.type_index();
	const TypeInfo* type_info = tryGetTypeInfo(type_index);
	if (!type_info) {
		StringHandle type_name_handle = type_spec.token().handle();
		if (!type_name_handle.isValid() && !type_spec.token().value().empty()) {
			type_name_handle = StringTable::getOrInternStringHandle(type_spec.token().value());
		}
		auto type_it = getTypesByNameMap().find(type_name_handle);
		if (type_it != getTypesByNameMap().end()) {
			type_info = type_it->second;
			if (type_info && !type_index.is_valid()) {
				type_index = type_info->type_index_.withCategory(type_info->typeEnum());
			}
		}
		if (!type_info) {
			return EvalResult::error("Constructor call has invalid struct/class type");
		}
	}

	const StructTypeInfo* struct_info = type_info->getStructInfo();
	if (!struct_info) {
		return EvalResult::error("Constructor call type is not a struct/class");
	}

	// Delegate to the shared helper for the find→bind→materialize sequence.
	auto ctor_result = try_materialize_struct_from_ctor_args(
		struct_info,
		type_index,
		ctor_call.arguments(),
		context,
		false,
		outer_bindings,
		ctor_call.resolved_constructor(),
		true);
	if (ctor_result.has_value()) {
		if (!ctor_result->success()) {
			return *ctor_result;
		}
		return std::move(*ctor_result);
	}

	// No matching constructor found - try aggregate initialization for aggregates.
	// This handles cases like Pt{3, 7}, and also empty list-initialization
	// such as true_type{} where the object has no non-static data members.
	if (!struct_info->hasUserDeclaredConstructor()) {
		// Convert arguments to InitializerListNode for aggregate initialization
		InitializerListNode init_list;
		for (size_t i = 0; i < ctor_call.arguments().size(); ++i) {
			const auto& arg = ctor_call.arguments()[i];
			init_list.add_initializer(arg);
		}
		auto agg_result = materialize_aggregate_object_value(struct_info, type_index, init_list, context, outer_bindings);
		if (agg_result.success()) {
			return agg_result;
		}
	} else if (ctor_call.arguments().size() > 0) {
		return EvalResult::error(std::string(StringBuilder()
			.append("No matching constructor for '"sv)
			.append(StringTable::getStringView(struct_info->getName()))
			.append("' with "sv)
			.append(std::to_string(ctor_call.arguments().size()))
			.append(" argument(s) in constexpr evaluation"sv)
			.commit()));
	}
	return EvalResult::error("No matching constructor found for constexpr object");
}

EvalResult Evaluator::materialize_array_value(
	TypeIndex element_type_index,
	const InitializerListNode& init_list,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* bindings) {
	auto count_brace_elision_scalar_clauses_for_type = [&](TypeIndex type_index, const auto& recurse) -> size_t {
		const TypeInfo* type_info = tryGetTypeInfo(type_index);
		const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
		if (!struct_info || struct_info->hasUserDeclaredConstructor()) {
			return 1;
		}

		size_t clause_count = 0;
		for (const auto& member : struct_info->members) {
			size_t member_clause_count = member.is_array
											? std::accumulate(
												  member.array_dimensions.begin(),
												  member.array_dimensions.end(),
												  size_t{1},
												  std::multiplies<size_t>())
											: size_t{1};
			const TypeInfo* member_type_info = tryGetTypeInfo(member.type_index);
			const StructTypeInfo* member_struct_info = member_type_info ? member_type_info->getStructInfo() : nullptr;
			if (member_struct_info && !member_struct_info->hasUserDeclaredConstructor()) {
				member_clause_count *= recurse(member.type_index, recurse);
			}
			clause_count += member_clause_count;
		}
		return clause_count > 0 ? clause_count : 1;
	};
	auto materialize_struct_array_element = [&](const InitializerListNode& element_init_list) -> EvalResult {
		const TypeInfo* element_type_info = tryGetTypeInfo(element_type_index);
		const StructTypeInfo* element_struct_info = element_type_info ? element_type_info->getStructInfo() : nullptr;
		if (!element_struct_info) {
			return EvalResult::error("Array element type is not a struct");
		}

		ChunkedVector<ASTNode> ctor_args;
		for (const auto& arg : element_init_list.initializers()) {
			ctor_args.push_back(arg);
		}
		if (auto ctor_result = try_materialize_struct_from_ctor_args(
				element_struct_info,
				element_type_index,
				ctor_args,
				context,
				false,
				bindings,
				nullptr,
				false)) {
			return std::move(*ctor_result);
		}
		return materialize_aggregate_object_value(
			element_struct_info,
			element_type_index,
			element_init_list,
			context,
			bindings);
	};

	std::vector<EvalResult> array_elements;
	array_elements.reserve(init_list.initializers().size());
	std::vector<int64_t> array_values;
	bool all_scalar_elements = true;

	const TypeInfo* element_type_info = tryGetTypeInfo(element_type_index);
	const StructTypeInfo* element_struct_info = element_type_info ? element_type_info->getStructInfo() : nullptr;
	const size_t brace_elision_scalar_clause_count =
		element_struct_info ? count_brace_elision_scalar_clauses_for_type(element_type_index, count_brace_elision_scalar_clauses_for_type) : 1;
	const auto& initializers = init_list.initializers();
	for (size_t cursor = 0; cursor < initializers.size();) {
		const ASTNode& element = initializers[cursor];
		EvalResult element_result;
		const ConstructorCallNode* direct_ctor = nullptr;
		if (element.is<ConstructorCallNode>()) {
			direct_ctor = &element.as<ConstructorCallNode>();
		} else if (element.is<ExpressionNode>()) {
			const ExpressionNode& element_expr = element.as<ExpressionNode>();
			direct_ctor = std::get_if<ConstructorCallNode>(&element_expr);
		}
		if (direct_ctor) {
			element_result = materialize_constructor_object_value(*direct_ctor, context, bindings);
			cursor++;
		} else if (element_struct_info) {
			if (element.is<InitializerListNode>()) {
				element_result = materialize_struct_array_element(element.as<InitializerListNode>());
				cursor++;
			} else {
				EvalResult direct_element_result = bindings
					? evaluate_expression_with_bindings_const(element, *bindings, context)
					: evaluate(element, context);
				if (direct_element_result.success() &&
					direct_element_result.object_type_index.is_valid() &&
					(!element_type_index.is_valid() || direct_element_result.object_type_index == element_type_index)) {
					element_result = std::move(direct_element_result);
					cursor++;
				} else {
					InitializerListNode element_init_list;
					size_t consumed = 0;
					while (consumed < brace_elision_scalar_clause_count &&
						   cursor < initializers.size() &&
						   !initializers[cursor].is<InitializerListNode>()) {
						element_init_list.add_initializer(initializers[cursor]);
						cursor++;
						consumed++;
					}
					if (consumed == 0) {
						return EvalResult::error("Expected initializer for struct array element");
					}
					element_result = materialize_struct_array_element(element_init_list);
				}
			}
		} else if (element.is<InitializerListNode>()) {
			// Nested array element (e.g., each row of int[2][3]): recurse with same element type.
			element_result = materialize_array_value(
				element_type_index,
				element.as<InitializerListNode>(),
				context, bindings);
			cursor++;
		} else if (bindings) {
			element_result = evaluate_expression_with_bindings_const(element, *bindings, context);
			cursor++;
		} else {
			element_result = evaluate(element, context);
			cursor++;
		}

		if (!element_result.success()) {
			return element_result;
		}

		if (element_result.object_type_index.is_valid() || element_result.is_array ||
			element_result.callable_var_decl != nullptr || element_result.callable_lambda != nullptr) {
			all_scalar_elements = false;
		} else if (std::get_if<double>(&element_result.value) == nullptr) {
			array_values.push_back(element_result.as_int());
		}

		array_elements.push_back(std::move(element_result));
	}

	EvalResult array_result;
	array_result.error_type = EvalErrorType::None;
	array_result.is_array = true;
	array_result.array_elements = std::move(array_elements);
	if (all_scalar_elements) {
		array_result.array_values = std::move(array_values);
	}
	return array_result;
}


EvalResult Evaluator::materialize_array_value_with_spec(
	const TypeSpecifierNode& type_spec,
	const InitializerListNode& init_list,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* bindings) {

	const auto& dims = type_spec.array_dimensions();
	if (dims.size() <= 1) {
		// Single-dimension or unspecified: delegate to the base overload.
		auto base_result = materialize_array_value(type_spec.type_index(), init_list, context, bindings);
		// If the declared dimension is known and larger than the init-list, zero-fill the tail.
		if (base_result.success() && dims.size() == 1 && dims[0] > 0 && base_result.is_array) {
			size_t declared_size = dims[0];
			TypeCategory elem_type = type_spec.type();
			base_result.array_elements.reserve(declared_size);
			if (!base_result.array_values.empty() && !isFloatingPointType(elem_type)) {
				base_result.array_values.reserve(declared_size);
			}
			while (base_result.array_elements.size() < declared_size) {
				EvalResult zero_elem;
				if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(type_spec.type_index())) {
					InitializerListNode empty_init_list;
					ChunkedVector<ASTNode> ctor_args;
					if (auto ctor_result = try_materialize_struct_from_ctor_args(
							struct_info,
							type_spec.type_index(),
							ctor_args,
							context,
							false,
							bindings,
							nullptr,
							false)) {
						zero_elem = std::move(*ctor_result);
					} else {
						zero_elem = materialize_aggregate_object_value(
							struct_info,
							type_spec.type_index(),
							empty_init_list,
							context,
							bindings);
					}
					if (!zero_elem.success()) {
						return zero_elem;
					}
				} else if (isFloatingPointType(elem_type)) {
					zero_elem = EvalResult::from_double(0.0);
				} else if (isUnsignedIntegralType(elem_type)) {
					zero_elem = EvalResult::from_uint(0ULL);
				} else {
					zero_elem = EvalResult::from_int(0LL);
				}
				if (!base_result.array_values.empty() && !isFloatingPointType(elem_type)) {
					base_result.array_values.push_back(0LL);
				}
				// Floating-point arrays intentionally keep array_values empty because the
				// legacy int64_t cache cannot preserve IEEE-754 element values.
				base_result.array_elements.push_back(std::move(zero_elem));
			}
		}
		return base_result;
	}

	// Multi-dimensional array (e.g., int[2][3]).
	// dims[0]    = outer element count (number of rows)
	// dims[1..N] = inner dimensions (size of each row)
	size_t outer_size = dims[0];
	// Build a TypeSpecifierNode for the inner (element) type.
	TypeSpecifierNode inner_type_spec = type_spec;
	inner_type_spec.set_array_dimensions(dims.subspan(1));

	// Check if the init_list is "fully flat" — non-empty and all elements are scalars
	// (no nested InitializerListNode).
	// Per C++20 dcl.init.aggr, a fully-flat list distributes scalars sequentially across inner
	// dimensions (brace-elision): e.g. int[2][3] = {1,2,3,4,5,6} → {{1,2,3},{4,5,6}}.
	bool is_fully_flat = false;
	const size_t initializer_count = init_list.size();
	if (initializer_count > 0) {
		is_fully_flat = true;
		for (size_t k = 0; k < initializer_count; ++k) {
			if (init_list.initializers()[k].is<InitializerListNode>()) {
				is_fully_flat = false;
				break;
			}
		}
	}

	std::vector<EvalResult> elements;
	elements.reserve(outer_size);

	if (is_fully_flat) {
		// Compute the number of scalar elements each inner array consumes.
		size_t inner_size = 1;
		for (size_t d : dims.subspan(1))
			inner_size *= d;

		size_t scalar_cursor = 0;
		for (size_t i = 0; i < outer_size; ++i) {
			EvalResult elem;
			if (scalar_cursor >= init_list.size()) {
				// No more scalars: zero-initialise.
				elem = make_zero_array_for_dims(dims.subspan(1), type_spec.type());
			} else {
				// Build a sub-init-list consuming up to inner_size scalars from the flat list.
				InitializerListNode sub_init;
				for (size_t j = 0; j < inner_size && scalar_cursor < init_list.size(); ++j, ++scalar_cursor) {
					sub_init.add_initializer(init_list.initializers()[scalar_cursor]);
				}
				elem = materialize_array_value_with_spec(inner_type_spec, sub_init, context, bindings);
			}
			if (!elem.success()) {
				return elem;
			}
			elements.push_back(std::move(elem));
		}
	} else {
		size_t inner_size = 1;
		for (size_t d : dims.subspan(1))
			inner_size *= d;

		size_t cursor = 0;
		const auto& initializers = init_list.initializers();
		for (size_t i = 0; i < outer_size; ++i) {
			EvalResult elem;
			if (cursor < initializer_count) {
				const ASTNode& initializer = initializers[cursor];
				if (initializer.is<InitializerListNode>()) {
					// Nested brace-init list for inner array ({…} form).
					elem = materialize_array_value_with_spec(
						inner_type_spec,
						initializer.as<InitializerListNode>(),
						context, bindings);
					cursor++;
				} else {
					// Mixed scalar/nested brace-init list: consume up to one inner array's worth of
					// consecutive scalars, stopping before the next nested brace list.
					InitializerListNode sub_init;
					size_t consumed = 0;
					while (consumed < inner_size && cursor < initializer_count &&
						   !initializers[cursor].is<InitializerListNode>()) {
						sub_init.add_initializer(initializers[cursor]);
						cursor++;
						consumed++;
					}
					elem = materialize_array_value_with_spec(inner_type_spec, sub_init, context, bindings);
				}
			} else {
				// Missing initializer: zero-initialise the entire inner array.
				elem = make_zero_array_for_dims(dims.subspan(1), type_spec.type());
			}
			if (!elem.success()) {
				return elem;
			}
			elements.push_back(std::move(elem));
		}
	}

	EvalResult result;
	result.is_array = true;
	result.array_elements = std::move(elements);
	return result;
}

EvalResult Evaluator::bind_members_from_initializer_list(
	const StructTypeInfo* struct_info,
	const InitializerListNode& init_list,
	std::unordered_map<std::string_view, EvalResult>& bindings,
	EvaluationContext& context,
	const std::unordered_map<std::string_view, EvalResult>* evaluation_bindings) {
	// Bind members covered by the initializer list.
	for (size_t mi = 0; mi < struct_info->members.size() && mi < init_list.size(); ++mi) {
		std::string_view mname;
		const StructMember* member_info = nullptr;
		if (init_list.is_designated(mi)) {
			// Designated initializer: use the member name from the designator
			mname = StringTable::getStringView(init_list.member_name(mi));
			member_info = struct_info->findMember(mname);
		} else {
			// Positional initializer: use the struct member at this index
			mname = StringTable::getStringView(struct_info->members[mi].getName());
			member_info = &struct_info->members[mi];
		}

		const ASTNode& initializer = init_list.initializers()[mi];
		if (member_info) {
			EvalResult val;
			const TypeInfo* member_type_info = tryGetTypeInfo(member_info->type_index);
			const bool is_struct_brace_init =
				!member_info->is_array &&
				initializer.is<InitializerListNode>() &&
				(is_struct_type(member_info->type_index.category())) &&
				member_type_info != nullptr;
			if (member_info->is_array && initializer.is<InitializerListNode>()) {
				// Nested InitializerListNode for array member (e.g., `return {{1,2,3}}`)
				const InitializerListNode& member_init_list = initializer.as<InitializerListNode>();
				val = Evaluator::materialize_array_value(
					member_info->type_index,
					member_init_list,
					context,
					evaluation_bindings);
			} else if (is_struct_brace_init) {
				// Nested InitializerListNode for a struct member — use aggregate materializer
				// so that nested struct init (e.g. Outer{{40}}) works with or without bindings.
				const InitializerListNode& member_init_list = initializer.as<InitializerListNode>();
				if (const StructTypeInfo* member_struct_info = member_type_info->getStructInfo()) {
					ChunkedVector<ASTNode> ctor_args;
					for (const auto& arg : member_init_list.initializers()) {
						ctor_args.push_back(arg);
					}
					if (auto ctor_result = Evaluator::try_materialize_struct_from_ctor_args(
							member_struct_info,
							member_info->type_index,
							ctor_args,
							context,
							false,
							evaluation_bindings,
							nullptr,
							false)) {
						val = std::move(*ctor_result);
					} else {
						val = Evaluator::materialize_aggregate_object_value(
							member_struct_info,
							member_info->type_index,
							member_init_list,
							context,
							evaluation_bindings);
					}
				} else {
					val = EvalResult::error("Member struct type not found for nested brace-init");
				}
			} else if (evaluation_bindings) {
				val = Evaluator::evaluate_expression_with_bindings_const(
					initializer,
					*evaluation_bindings,
					context);
				if (val.success()) {
					val = applyAggregateMemberScalarInitialization(
						*member_info,
						std::move(val),
						init_list.is_brace_init());
				}
			} else {
				val = materialize_member_initializer_value(
					*member_info,
					initializer,
					context,
					nullptr,
					init_list.is_brace_init());
			}
			if (!val.success())
				return val;
			bindings[mname] = std::move(val);
			continue;
		}

		auto val = evaluation_bindings
					   ? Evaluator::evaluate_expression_with_bindings_const(initializer, *evaluation_bindings, context)
					   : evaluate(initializer, context);
		if (!val.success())
			return val;
		// member_info is always null here: the `if (member_info) { ... continue; }`
		// block above handles all cases where the member was found.  Reaching this
		// point means the initializer has no matching struct member.
		return EvalResult::error(
			"Excess or unrecognized initializer in aggregate initialization "
			"(no matching member for positional initializer)");
	}
	// Apply default member initializers for remaining members.
	for (size_t mi = 0; mi < struct_info->members.size(); ++mi) {
		const auto& member = struct_info->members[mi];
		std::string_view mname = StringTable::getStringView(member.getName());
		if (bindings.find(mname) == bindings.end() && member.default_initializer.has_value()) {
			EvalResult default_result;
			const ASTNode& default_initializer = member.default_initializer.value();
			std::unordered_map<std::string_view, EvalResult> default_bindings;
			if (evaluation_bindings) {
				default_bindings = *evaluation_bindings;
			}
			for (const auto& [name, value] : bindings) {
				default_bindings[name] = value;
			}
			if (default_initializer.is<InitializerListNode>()) {
				default_result = materialize_member_initializer_value(
					member,
					default_initializer,
					context,
					&default_bindings,
					false);
			} else {
				default_result = Evaluator::evaluate_expression_with_bindings_const(
					default_initializer,
					default_bindings,
					context);
			}
			if (default_result.success() && !default_initializer.is<InitializerListNode>()) {
				default_result = applyAggregateMemberScalarInitialization(
					member,
					std::move(default_result),
					false);
			}
			if (!default_result.success())
				return default_result;
			bindings[mname] = default_result;
		}
	}
	return EvalResult::from_bool(true);
}

EvalResult Evaluator::bind_members_from_constructor_initializers(
	const StructTypeInfo* struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
	std::unordered_map<std::string_view, EvalResult>& member_bindings,
	EvaluationContext& context,
	bool ignore_default_initializer_errors) {

	// Returns a type-correct zero EvalResult for a given element type.
	// Floating-point types get 0.0, unsigned types get 0u, signed/bool get 0.
	auto make_zero_element = [](TypeCategory element_type) -> EvalResult {
		if (isFloatingPointType(element_type)) {
			return EvalResult::from_double(0.0);
		}
		if (isUnsignedIntegralType(element_type)) {
			return EvalResult::from_uint(0ULL);
		}
		return EvalResult::from_int(0LL);
	};

	for (const auto& mem_init : ctor_decl.member_initializers()) {
		// Handle multi-arg brace-init (e.g., arr{a, b, c}) stored as an InitializerListNode.
		// This is used for array members and aggregate struct members.
		if (mem_init.initializer_expr.is<InitializerListNode>()) {
			const InitializerListNode& init_list = mem_init.initializer_expr.as<InitializerListNode>();
			const StructMember* member_info = struct_info ? struct_info->findMember(mem_init.member_name) : nullptr;
			if (!member_info) {
				return EvalResult::error("Member '" + std::string(mem_init.member_name) +
										 "' not found for brace-init in constexpr constructor");
			}
			EvalResult member_result;
			if (member_info->is_array) {
				member_result = materialize_array_value(
					member_info->type_index, init_list, context, &ctor_param_bindings);
				// C++ aggregate init: zero-fill remaining elements up to the declared array size
				// using a type-correct zero for each native type.
				if (member_result.success() && !member_info->array_dimensions.empty()) {
					size_t declared_size = member_info->array_dimensions[0];
					while (member_result.array_elements.size() < declared_size) {
						member_result.array_elements.push_back(make_zero_element(member_info->memberType()));
					}
					// Only extend array_values (legacy int64_t fallback) for integer types.
					if (!member_result.array_values.empty() && !isFloatingPointType(member_info->memberType())) {
						while (member_result.array_values.size() < declared_size) {
							member_result.array_values.push_back(0);
						}
					}
				}
			} else if (is_struct_type(member_info->type_index.category())) {
				if (const TypeInfo* member_type_info = tryGetTypeInfo(member_info->type_index);
					const StructTypeInfo* member_struct_info = member_type_info ? member_type_info->getStructInfo() : nullptr) {
					ChunkedVector<ASTNode> ctor_args;
					for (const auto& arg : init_list.initializers()) {
						ctor_args.push_back(arg);
					}
					if (auto ctor_result = try_materialize_struct_from_ctor_args(
							member_struct_info,
							member_info->type_index,
							ctor_args,
							context,
							false,
							&ctor_param_bindings,
							nullptr,
							false)) {
						member_result = std::move(*ctor_result);
					} else {
						member_result = materialize_aggregate_object_value(
							member_struct_info, member_info->type_index, init_list, context, &ctor_param_bindings);
					}
				} else {
					member_result = EvalResult::error("Member struct type not found for brace-init");
				}
			} else if (init_list.size() == 0) {
				// Empty brace-init on scalar member (e.g., int x{}): value-initialize to zero.
				member_result = make_zero_element(member_info->memberType());
			} else {
				return EvalResult::error("Brace-init list used on non-array, non-struct member '" +
										 std::string(mem_init.member_name) + "' in constexpr constructor");
			}
			if (!member_result.success()) {
				return member_result;
			}
			member_bindings[mem_init.member_name] = std::move(member_result);
			continue;
		}
		auto member_result = evaluate_expression_with_bindings(mem_init.initializer_expr, ctor_param_bindings, context);
		if (!member_result.success()) {
			return member_result;
		}
		// Handle paren-init for a struct member: inner(v) where inner is a struct type.
		// The parser stores the single argument as a plain expression, so evaluating it
		// gives a scalar (object_type_index is invalid for scalar results).  We need to
		// materialize the nested struct by calling its constructor with that argument so
		// that object_member_bindings are populated for later nested member access.
		if (struct_info && !member_result.object_type_index.is_valid() && !member_result.is_array) {
			if (const StructMember* member_info = struct_info->findMember(mem_init.member_name)) {
				if (!member_info->is_array && is_struct_type(member_info->type_index.category())) {
					const TypeInfo* member_type_info = tryGetTypeInfo(member_info->type_index);
					const StructTypeInfo* member_struct_info = member_type_info ? member_type_info->getStructInfo() : nullptr;
					if (member_struct_info) {
						ChunkedVector<ASTNode> ctor_args;
						ctor_args.push_back(mem_init.initializer_expr);
						if (auto ctor_result = try_materialize_struct_from_ctor_args(
								member_struct_info,
								member_info->type_index,
								ctor_args,
								context,
								false,
								&ctor_param_bindings,
								nullptr,
								false)) {
							if (ctor_result->success()) {
								member_result = std::move(*ctor_result);
							}
						}
					}
				}
			}
		}
		// Handle single-element brace-init for array members (e.g., arr{val} for int arr[3]).
		// The parser stores arr{val} as a scalar (init_args[0]) because there is only one arg.
		// C++ requires: arr[0] = val, arr[1..n-1] = zero-initialized for the element type.
		if (struct_info && !member_result.is_array) {
			if (const StructMember* member_info = struct_info->findMember(mem_init.member_name)) {
				if (member_info->is_array) {
					size_t array_size = member_info->array_dimensions.empty() ? 1 : member_info->array_dimensions[0];
					EvalResult array_r;
					array_r.error_type = EvalErrorType::None;
					array_r.is_array = true;
					array_r.array_elements.push_back(std::move(member_result));
					for (size_t i = 1; i < array_size; ++i) {
						array_r.array_elements.push_back(make_zero_element(member_info->memberType()));
					}
					// Do not populate array_values for floating-point elements since array_values
					// is int64_t and cannot represent doubles without truncation. array_elements
					// is the authoritative source and is always checked first during subscript.
					if (!isFloatingPointType(member_info->memberType())) {
						for (size_t i = 0; i < array_size; ++i) {
							array_r.array_values.push_back(
								i == 0 ? array_r.array_elements[0].as_int() : 0LL);
						}
					}
					member_result = std::move(array_r);
				}
			}
		}
		member_bindings[mem_init.member_name] = std::move(member_result);
	}

	for (const auto& member : struct_info->members) {
		std::string_view member_name = StringTable::getStringView(member.getName());
		if (member_bindings.find(member_name) != member_bindings.end() || !member.default_initializer.has_value()) {
			continue;
		}

		auto default_result = evaluate(member.default_initializer.value(), context);
		if (!default_result.success()) {
			if (ignore_default_initializer_errors) {
				continue;
			}
			return default_result;
		}

		member_bindings[member_name] = default_result;
	}

	return EvalResult::from_bool(true);
}

EvalResult Evaluator::materialize_members_from_constructor(
	const StructTypeInfo* struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
	std::unordered_map<std::string_view, EvalResult>& member_bindings,
	EvaluationContext& context,
	bool ignore_default_initializer_errors) {
	auto* saved_local_bindings = context.local_bindings;

	// Handle delegating constructors: S() : S(42) {} — the delegation performs
	// full construction; the delegating constructor's own body runs afterwards.
	if (const auto& del_init = ctor_decl.delegating_initializer(); del_init.has_value()) {
		if (context.current_depth >= context.max_recursion_depth) {
			return EvalResult::error("Constexpr recursion depth limit exceeded in delegating constructor");
		}
		context.current_depth++;
		auto depth_guard = ScopeGuard([&context]() { context.current_depth--; });
		ChunkedVector<ASTNode> del_args;
		for (const auto& arg : del_init->arguments) {
			del_args.push_back(arg);
		}
		auto del_result = try_materialize_struct_from_ctor_args(
			struct_info,
			ctor_decl.owning_type_index().is_valid()
				? ctor_decl.owning_type_index()
				: (struct_info->own_type_index_.has_value() ? *struct_info->own_type_index_ : TypeIndex{}),
			del_args,
			context,
			false,
			&ctor_param_bindings,
			nullptr,
			ignore_default_initializer_errors);
		if (!del_result.has_value()) {
			return EvalResult::error("Delegating constructor: no matching constructor found for delegation");
		}
		if (!del_result->success()) {
			return *del_result;
		}
		// Copy member bindings from the delegated-to constructor result
		member_bindings = std::move(del_result->object_member_bindings);
		// Now execute the delegating constructor's own body (if any)
		const auto& ctor_definition = ctor_decl.get_definition();
		if (!ctor_definition.has_value() || !ctor_definition->is<BlockNode>()) {
			return EvalResult::from_bool(true);
		}
		std::unordered_map<std::string_view, EvalResult> ctor_body_bindings = member_bindings;
		std::unordered_map<std::string_view, EvalResult> ctor_local_bindings = ctor_param_bindings;
		const StructTypeInfo* saved_struct_info = context.struct_info;
		TypeIndex saved_struct_type_index = context.struct_type_index;
		context.local_bindings = &ctor_local_bindings;
		context.struct_info = struct_info;
		if (ctor_decl.owning_type_index().is_valid()) {
			context.struct_type_index = ctor_decl.owning_type_index();
		} else if (struct_info && struct_info->own_type_index_.has_value()) {
			context.struct_type_index = *struct_info->own_type_index_;
		}
		auto body_result = evaluate_block_with_bindings(
			*ctor_definition,
			ctor_body_bindings,
			context,
			"Delegating constructor body is not a block",
			"");
		context.local_bindings = saved_local_bindings;
		context.struct_info = saved_struct_info;
		context.struct_type_index = saved_struct_type_index;
		// Constructor body returns void; a "no return value" result (empty error) is normal.
		if (!body_result.success() && !body_result.error_message.empty()) {
			return body_result;
		}
		for (const auto& member : struct_info->members) {
			std::string_view member_name = StringTable::getStringView(member.getName());
			auto it = ctor_body_bindings.find(member_name);
			if (it != ctor_body_bindings.end()) {
				member_bindings[member_name] = it->second;
			}
		}
		return EvalResult::from_bool(true);
	}

	auto materializeBaseInitializers = [&]() -> EvalResult {
		for (const auto& base_init : ctor_decl.base_initializers()) {
			const BaseClassSpecifier* base_spec = nullptr;
			for (const auto& candidate : struct_info->base_classes) {
				if (StringTable::getOrInternStringHandle(candidate.name) == base_init.getBaseClassName()) {
					base_spec = &candidate;
					break;
				}
			}
			if (!base_spec) {
				return EvalResult::error("Base initializer not found in constexpr constructor");
			}

			const TypeInfo* base_type_info = tryGetTypeInfo(base_spec->type_index);
			const StructTypeInfo* base_struct_info = base_type_info ? base_type_info->getStructInfo() : nullptr;
			if (!base_struct_info) {
				return EvalResult::error("Base initializer type is not a struct in constexpr constructor");
			}

			ChunkedVector<ASTNode> base_args;
			for (const auto& arg : base_init.arguments) {
				base_args.push_back(arg);
			}

			EvalResult base_result;
			if (auto ctor_result = try_materialize_struct_from_ctor_args(
					base_struct_info,
					base_spec->type_index,
					base_args,
					context,
					false,
					&ctor_param_bindings,
					nullptr,
					false)) {
				base_result = std::move(*ctor_result);
			} else {
				InitializerListNode init_list;
				for (const auto& arg : base_init.arguments) {
					init_list.add_initializer(arg);
				}
				base_result = materialize_aggregate_object_value(
					base_struct_info,
					base_spec->type_index,
					init_list,
					context,
					&ctor_param_bindings);
			}

			if (!base_result.success()) {
				return base_result;
			}

			for (const auto& [member_name, member_value] : base_result.object_member_bindings) {
				member_bindings[member_name] = member_value;
			}
		}

		return EvalResult::from_bool(true);
	};

	auto base_bind_result = materializeBaseInitializers();
	if (!base_bind_result.success()) {
		return base_bind_result;
	}

	for (const auto& base : struct_info->base_classes) {
		bool has_explicit_base_initializer = false;
		StringHandle base_name = StringTable::getOrInternStringHandle(base.name);
		for (const auto& base_init : ctor_decl.base_initializers()) {
			if (base_name == base_init.getBaseClassName()) {
				has_explicit_base_initializer = true;
				break;
			}
		}
		if (has_explicit_base_initializer) {
			continue;
		}

		EvalResult base_result = makeConstructorDefaultInitFromType(makeTypeSpecForDefaultInit(base.type_index), context);
		if (!base_result.success()) {
			return base_result;
		}
		for (const auto& [base_member_name, base_member_value] : base_result.object_member_bindings) {
			if (member_bindings.find(base_member_name) == member_bindings.end()) {
				member_bindings[base_member_name] = base_member_value;
			}
		}
	}

	context.local_bindings = &ctor_param_bindings;
	auto member_bind_result = bind_members_from_constructor_initializers(
		struct_info,
		ctor_decl,
		ctor_param_bindings,
		member_bindings,
		context,
		ignore_default_initializer_errors);
	context.local_bindings = saved_local_bindings;
	if (!member_bind_result.success()) {
		return member_bind_result;
	}

	for (const auto& member : struct_info->members) {
		std::string_view member_name = StringTable::getStringView(member.getName());
		if (member_bindings.find(member_name) != member_bindings.end()) {
			continue;
		}

		EvalResult default_result = makeConstructorMemberDefaultInit(member, context);
		if (!default_result.success()) {
			return default_result;
		}
		member_bindings[member_name] = std::move(default_result);
	}

	const auto& ctor_definition = ctor_decl.get_definition();
	if (!ctor_definition.has_value() || !ctor_definition->is<BlockNode>()) {
		return EvalResult::from_bool(true);
	}

	std::unordered_map<std::string_view, EvalResult> ctor_body_bindings = member_bindings;
	std::unordered_map<std::string_view, EvalResult> ctor_local_bindings = ctor_param_bindings;
	saved_local_bindings = context.local_bindings;
	const StructTypeInfo* saved_struct_info = context.struct_info;
	TypeIndex saved_struct_type_index = context.struct_type_index;
	auto restoreCtorBodyContext = [&]() {
		context.local_bindings = saved_local_bindings;
		context.struct_info = saved_struct_info;
		context.struct_type_index = saved_struct_type_index;
	};
	context.local_bindings = &ctor_local_bindings;
	context.struct_info = struct_info;
	if (ctor_decl.owning_type_index().is_valid()) {
		context.struct_type_index = ctor_decl.owning_type_index();
	} else if (struct_info && struct_info->own_type_index_.has_value()) {
		context.struct_type_index = *struct_info->own_type_index_;
	}
	EvalResult this_binding = EvalResult::from_int(0LL);
	this_binding.object_type_index = context.struct_type_index;
	this_binding.object_member_bindings = member_bindings;
	ctor_body_bindings["this"] = std::move(this_binding);

	const BlockNode& ctor_body = ctor_definition->as<BlockNode>();
	for (const auto& ctor_stmt : ctor_body.get_statements()) {
		auto stmt_result = evaluate_statement_with_bindings(ctor_stmt, ctor_body_bindings, context);
		if (stmt_result.success()) {
			restoreCtorBodyContext();
			break;
		}
		if (stmt_result.error_message != "Statement executed (not a return)") {
			restoreCtorBodyContext();
			return stmt_result;
		}
	}
	restoreCtorBodyContext();
	ctor_body_bindings.erase("this");

	for (const auto& member : struct_info->members) {
		std::string_view member_name = StringTable::getStringView(member.getName());
		auto it = ctor_body_bindings.find(member_name);
		if (it != ctor_body_bindings.end()) {
			member_bindings[member_name] = it->second;
		}
	}

	return EvalResult::from_bool(true);
}

std::optional<EvalResult> Evaluator::try_evaluate_member_from_constructor_initializers(
	const StructTypeInfo* struct_info,
	const ConstructorDeclarationNode& ctor_decl,
	std::unordered_map<std::string_view, EvalResult>& ctor_param_bindings,
	std::string_view member_name,
	EvaluationContext& context) {
	std::unordered_map<std::string_view, EvalResult> member_bindings;
	auto materialize_result = materialize_members_from_constructor(
		struct_info,
		ctor_decl,
		ctor_param_bindings,
		member_bindings,
		context,
		true);
	if (!materialize_result.success()) {
		return materialize_result;
	}

	auto member_it = member_bindings.find(member_name);
	if (member_it != member_bindings.end()) {
		return member_it->second;
	}

	return std::nullopt;
}

// Attempt to materialize a struct object by finding and invoking a matching user-defined
// constructor with the given arguments.  Returns std::nullopt when no matching constructor
// exists (caller may fall back to aggregate initialization).  Returns an EvalResult
// (success or error) when a constructor candidate was found and materialization was attempted.
std::optional<EvalResult> Evaluator::try_materialize_struct_from_ctor_args(
	const StructTypeInfo* struct_info,
	TypeIndex type_index,
	const ChunkedVector<ASTNode>& args,
	EvaluationContext& context,
	bool skip_implicit_constructors,
	const std::unordered_map<std::string_view, EvalResult>* outer_bindings,
	const ConstructorDeclarationNode* resolved_ctor,
	bool ignore_default_initializer_errors) {
	const ConstructorDeclarationNode* matching_ctor = resolved_ctor;
	if (!matching_ctor) {
		matching_ctor =
			find_matching_constructor(
				struct_info, args, context, skip_implicit_constructors, outer_bindings);
	}
	if (!matching_ctor) {
		return std::nullopt;
	}

	// Implicit same-type copy/move constructors have no user-written member
	// initializer list/body to execute. Materialize them by reusing the
	// source constexpr object directly so nested member bindings are preserved.
	if (matching_ctor->is_implicit() && matching_ctor->parameter_nodes().size() == 1) {
		if (const ASTNode* single_initializer = tryGetSingleExpressionInitializer(args)) {
			EvalResult source_value = outer_bindings
				? evaluate_expression_with_bindings_const(*single_initializer, *outer_bindings, context)
				: evaluate(*single_initializer, context);
			if (!source_value.success()) {
				return source_value;
			}
			if (canReuseConstexprSameTypeObjectValue(source_value, type_index)) {
				return source_value;
			}
		}
	}

	EvalResult object_result = EvalResult::from_int(0LL);
	object_result.object_type_index = type_index;

	std::unordered_map<std::string_view, EvalResult> ctor_param_bindings;
	auto bind_result = bind_evaluated_arguments(
		matching_ctor->parameter_nodes(),
		args,
		ctor_param_bindings,
		context,
		"Invalid parameter in constexpr struct construction",
		outer_bindings,
		true);
	if (!bind_result.success()) {
		return bind_result;
	}

	auto materialize_result = materialize_members_from_constructor(
		struct_info, *matching_ctor, ctor_param_bindings,
		object_result.object_member_bindings, context, ignore_default_initializer_errors);
	if (!materialize_result.success()) {
		return materialize_result;
	}

	// Callers that return this value directly rely on the helper preserving the object type.
	assert(object_result.object_type_index == type_index);
	return object_result;
}

// Helper to extract member values from a constexpr object
EvalResult Evaluator::extract_object_members(
	const ASTNode& object_expr,
	std::unordered_map<std::string_view, EvalResult>& member_bindings,
	EvaluationContext& context) {

	// Get the object variable name
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;

	auto extracted = extract_identifier_from_expression(object_expr);
	if (!extracted) {
		return EvalResult::error("Complex object expressions not yet supported in constexpr member function calls");
	}
	object_identifier = extracted->identifier;
	var_name = extracted->name;

	const VariableDeclarationNode* var_decl = nullptr;
	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
			object_identifier,
			var_name,
			context,
			"member function call",
			resolved_object)) {
		return *resolve_error;
	}

	var_decl = resolved_object.var_decl;
	if (var_decl && !var_decl->is_constexpr()) {
		return EvalResult::error("Variable in member function call must be constexpr: " + std::string(var_name));
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex declared_type_index = resolved_object.declared_type_index;
	if (!initializer->has_value()) {
		return EvalResult::error("Constexpr variable has no initializer: " + std::string(var_name));
	}

	// Handle brace-initialized objects (aggregate init): extract member values by position.
	if ((*initializer)->is<InitializerListNode>()) {
		const TypeInfo* declared_type_info = tryGetTypeInfo(declared_type_index);
		if (!declared_type_info) {
			return EvalResult::error("Brace-initialized object has invalid type");
		}
		const StructTypeInfo* agg_struct_info = declared_type_info->getStructInfo();
		if (!agg_struct_info)
			return EvalResult::error("Brace-initialized object is not a struct");
		const InitializerListNode& init_list = (*initializer)->as<InitializerListNode>();
		return bind_members_from_initializer_list(agg_struct_info, init_list, member_bindings, context);
	}

	const ConstructorCallNode* ctor_call_ptr = extract_constructor_call(*initializer);
	if (!ctor_call_ptr) {
		return EvalResult::error("Member function calls require struct/class objects");
	}

	const ConstructorCallNode& ctor_call = *ctor_call_ptr;

	// Get the struct type info
	const TypeSpecifierNode& type_spec = ctor_call.type_node();

	if (!is_struct_type(type_spec.category())) {
		return EvalResult::error("Member function call requires a struct type");
	}

	TypeIndex type_index = type_spec.type_index();
	const TypeInfo* struct_type_info = nullptr;
	const StructTypeInfo* struct_info = nullptr;
	struct_type_info = tryGetTypeInfo(type_index);
	if (struct_type_info) {
		struct_info = struct_type_info->getStructInfo();
	}
	if (!struct_info) {
		struct_type_info = tryGetTypeInfo(declared_type_index);
	}
	if (struct_type_info && !struct_info) {
		type_index = TypeIndex{declared_type_index};
		struct_info = struct_type_info->getStructInfo();
	}
	if (!struct_info) {
		return EvalResult::error("Type is not a struct in member function call");
	}

	auto ctor_result = materialize_constructor_object_value(ctor_call, context, nullptr);
	if (!ctor_result.success()) {
		return ctor_result;
	}

	member_bindings = std::move(ctor_result.object_member_bindings);
	return EvalResult::from_bool(true);	// Success
}

// Evaluate array subscript (e.g., arr[0] or obj.data[1] or ptr[i])
EvalResult Evaluator::evaluate_array_subscript(const ArraySubscriptNode& subscript, EvaluationContext& context) {
	// First, evaluate the index expression to get the constant index
	auto index_result = evaluate(subscript.index_expr(), context);
	if (!index_result.success()) {
		return index_result;
	}

	long long index = index_result.as_int();
	if (index < 0) {
		return EvalResult::error("Negative array index in constant expression");
	}

	// Get the array expression - this could be:
	// 1. A member access (e.g., obj.data)
	// 2. An identifier (e.g., arr or ptr)
	// 3. A pointer expression (e.g., (ptr + 1)[i] → *(ptr + 1 + i))
	const ASTNode& array_expr = subscript.array_expr();

	// Check if it's a member access (e.g., obj.data[0])
	if (array_expr.is<ExpressionNode>()) {
		const ExpressionNode& expr = array_expr.as<ExpressionNode>();
		if (const auto* member_access_ptr = std::get_if<MemberAccessNode>(&expr)) {
			return evaluate_member_array_subscript(*member_access_ptr, static_cast<size_t>(index), context);
		}
		if (const auto* identifier_ptr = std::get_if<IdentifierNode>(&expr)) {
			if (identifier_is_array_var(*identifier_ptr, context)) {
				return evaluate_variable_array_subscript(*identifier_ptr, static_cast<size_t>(index), context);
			}
			// Identifier is a pointer variable or local binding — fall through to pointer path.
		}
	}
	if (array_expr.is<IdentifierNode>()) {
		const IdentifierNode& id = array_expr.as<IdentifierNode>();
		if (identifier_is_array_var(id, context)) {
			return evaluate_variable_array_subscript(id, static_cast<size_t>(index), context);
		}
		// Identifier is a pointer variable or local binding — fall through to pointer path.
	}

	// Try evaluating the array expression — if it yields a pointer, treat ptr[i] as *(ptr + i).
	auto arr_result = evaluate(array_expr, context);
	if (arr_result.success() && arr_result.pointer_to_var.isValid()) {
		int64_t effective_offset = arr_result.pointer_offset + index;
		return dereference_constexpr_pointer(
			StringTable::getStringView(arr_result.pointer_to_var),
			context, effective_offset);
	}
	// Handle inline array results (e.g., string literal evaluated directly, or
	// constexpr const char* whose initializer evaluates to a string-char array).
	if (arr_result.success() && arr_result.is_array && !arr_result.array_elements.empty()) {
		if (static_cast<size_t>(index) >= arr_result.array_elements.size()) {
			return EvalResult::error("Array index out of bounds in constant expression");
		}
		return arr_result.array_elements[static_cast<size_t>(index)];
	}

	// Handle struct with operator[]: if arr_result is a struct object, look up operator[]
	// and dispatch to it (e.g., s[0] where s has constexpr char operator[](int i) const).
	if (arr_result.success() && arr_result.object_type_index.is_valid()) {
		const TypeInfo* type_info = tryGetTypeInfo(arr_result.object_type_index);
		const StructTypeInfo* struct_info = type_info ? type_info->getStructInfo() : nullptr;
		if (struct_info) {
			StringHandle op_bracket = StringTable::getOrInternStringHandle("operator[]");
			bool object_is_const = arr_result.exact_type.has_value() &&
				arr_result.exact_type->cv_qualifier() == CVQualifier::Const;
			if (!object_is_const) {
				const IdentifierNode* object_id = tryGetIdentifier(array_expr);
				if (object_id && context.symbols) {
					std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(
						object_id,
						object_id->name(),
						*context.symbols);
					if (!symbol_opt.has_value() && context.global_symbols) {
						symbol_opt = lookup_identifier_symbol(object_id, object_id->name(), *context.global_symbols);
					}
					if (symbol_opt.has_value() && symbol_opt->is<VariableDeclarationNode>()) {
						const VariableDeclarationNode& var_decl = symbol_opt->as<VariableDeclarationNode>();
						const TypeSpecifierNode& decl_type = var_decl.declaration().type_specifier_node();
						object_is_const = var_decl.is_constexpr() ||
							decl_type.cv_qualifier() == CVQualifier::Const;
					}
				}
			}
			if (!object_is_const) {
				auto expr_type = Evaluator::tryQueryExpressionType(array_expr, context);
				object_is_const = expr_type.has_value() &&
					expr_type->cv_qualifier() == CVQualifier::Const;
			}
			ResolvedMemberFunctionCandidate candidate =
				findConstexprOperatorOverload(struct_info, op_bracket, 1, context);
			if (candidate.ambiguous) {
				return EvalResult::error("Ambiguous operator[] overload in constant expression");
			}
			if (candidate.function) {
				return invokeConstexprMemberFunction(
					*candidate.function,
					arr_result.object_member_bindings,
					arr_result.object_type_index,
					type_info,
					struct_info,
					{index_result},
					context,
					"operator[] body is not a block",
					"operator[] did not return a value");
			}
		}
	}

	return EvalResult::error("Array subscript on unsupported expression type");
}

// Evaluate array subscript on a member (e.g., obj.data[0])
EvalResult Evaluator::evaluate_member_array_subscript(
	const MemberAccessNode& member_access,
	size_t index,
	EvaluationContext& context) {

	const ASTNode& object_expr = member_access.object();
	std::string_view member_name = member_access.member_name();
	std::string_view var_name;
	const IdentifierNode* object_identifier = nullptr;
	auto extract_indexed_array_value = [&](const EvalResult& array_value) -> EvalResult {
		if (!array_value.is_array) {
			return EvalResult::error("Array member is not initialized with an array value");
		}
		if (!array_value.array_elements.empty()) {
			if (index >= array_value.array_elements.size()) {
				return EvalResult::error(
					std::string(StringBuilder()
						.append("Array index "sv)
						.append(index)
						.append(" out of bounds (size "sv)
						.append(array_value.array_elements.size())
						.append(")"sv)
						.commit()));
			}
			return array_value.array_elements[index];
		}
		if (index >= array_value.array_values.size()) {
			return EvalResult::error(
				std::string(StringBuilder()
					.append("Array index "sv)
					.append(index)
					.append(" out of bounds (size "sv)
					.append(array_value.array_values.size())
					.append(")"sv)
					.commit()));
		}
		return EvalResult::from_int(array_value.array_values[index]);
	};
	auto try_evaluate_object_member_array = [&](const ASTNode& object_expression) -> std::optional<EvalResult> {
		EvalResult object_result = evaluate(object_expression, context);
		if (!object_result.success()) {
			return object_result;
		}
		auto member_it = object_result.object_member_bindings.find(member_name);
		if (member_it == object_result.object_member_bindings.end()) {
			return std::nullopt;
		}
		return extract_indexed_array_value(member_it->second);
	};

	if (const IdentifierNode* identifier = tryGetIdentifier(object_expr)) {
		object_identifier = identifier;
		var_name = identifier->name();
	} else if (auto evaluated_member = try_evaluate_object_member_array(object_expr)) {
		return *evaluated_member;
	} else {
		return EvalResult::error("Invalid object expression in array member access");
	}

	auto evaluate_array_member_element_from_initializer =
		[&](const std::optional<ASTNode>& initializer_opt, TypeIndex declared_type_index) -> EvalResult {
		ResolvedConstexprMemberSource resolved_member;
		if (auto resolve_error = resolve_constexpr_member_source_from_initializer(
				initializer_opt,
				declared_type_index,
				member_name,
				"array subscript",
				context,
				resolved_member)) {
			return *resolve_error;
		}

		if (resolved_member.value.has_value()) {
			return extract_indexed_array_value(resolved_member.value.value());
		}

		if (!resolved_member.initializer.has_value()) {
			return EvalResult::error("Internal error: unresolved array member source");
		}

		if (!resolved_member.initializer->is<InitializerListNode>()) {
			return EvalResult::error("Array member is not initialized with an array initializer");
		}

		const InitializerListNode& init_list = resolved_member.initializer->as<InitializerListNode>();
		std::optional<TypeSpecifierNode> member_type_spec;
		if (resolved_member.member_info && resolved_member.member_info->array_dimensions.size() > 1) {
			member_type_spec = makeArrayTypeSpec(
				resolved_member.member_info->type_index,
				resolved_member.member_info->array_dimensions);
		}
		if (auto materialized_row = tryMaterializeMultidimArrayRow(
				member_type_spec ? &*member_type_spec : nullptr,
				init_list,
				index,
				context)) {
			return *materialized_row;
		}
		const auto& elements = init_list.initializers();
		if (index >= elements.size()) {
			return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
		}

		if (!resolved_member.evaluation_bindings.empty()) {
			return evaluate_expression_with_bindings(
				elements[index],
				resolved_member.evaluation_bindings,
				context);
		}

		return evaluate(elements[index], context);
	};

	if (context.symbols) {
		StringHandle qualified_handle = StringTable::getOrInternStringHandle(
			StringBuilder().append(var_name).append("::"sv).append(member_name).commit());
		if (auto qualified_symbol = context.symbols->lookup(qualified_handle);
			qualified_symbol.has_value() && qualified_symbol->is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& qualified_var = qualified_symbol->as<VariableDeclarationNode>();
			if (!qualified_var.is_constexpr()) {
				return EvalResult::error("Static member array in array subscript must be constexpr");
			}
			const auto& qualified_initializer = qualified_var.initializer();
			if (!qualified_initializer.has_value() || !qualified_initializer->is<InitializerListNode>()) {
				return EvalResult::error("Static member array in array subscript must have an initializer list");
			}

			const InitializerListNode& init_list = qualified_initializer->as<InitializerListNode>();
			{
				const TypeSpecifierNode& type_spec = qualified_var.declaration().type_specifier_node();
				if (auto materialized_row = tryMaterializeMultidimArrayRow(&type_spec, init_list, index, context)) {
					return *materialized_row;
				}
			}
		}
	}

	ResolvedConstexprObject resolved_object;
	if (auto resolve_error = resolve_constexpr_object_source(
			object_identifier,
			var_name,
			context,
			"array subscript",
			resolved_object)) {
		return *resolve_error;
	}

	if (resolved_object.var_decl && !resolved_object.var_decl->is_constexpr()) {
		return EvalResult::error("Variable in array subscript must be constexpr");
	}

	const std::optional<ASTNode>* initializer = resolved_object.initializer;
	TypeIndex declared_type_index = resolved_object.declared_type_index;
	return evaluate_array_member_element_from_initializer(*initializer, declared_type_index);
}

// Evaluate array subscript on a variable (e.g., arr[0] where arr is constexpr)
EvalResult Evaluator::evaluate_variable_array_subscript(
	const IdentifierNode& identifier,
	size_t index,
	EvaluationContext& context) {
	std::string_view var_name = identifier.name();
	auto evaluate_array_initializer = [&](const std::optional<ASTNode>& initializer_opt,
										 TypeIndex element_type_index,
										 bool element_is_struct_object,
										 const TypeSpecifierNode* type_spec_opt) -> std::optional<EvalResult> {
		if (!initializer_opt.has_value() || !initializer_opt->is<InitializerListNode>()) {
			return std::nullopt;
		}

		const InitializerListNode& init_list = initializer_opt->as<InitializerListNode>();
		if (type_spec_opt) {
			EvalResult materialized = materialize_array_value_with_spec(*type_spec_opt, init_list, context, nullptr);
			if (!materialized.success()) {
				return materialized;
			}
			if (index >= materialized.array_elements.size()) {
				return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(materialized.array_elements.size()) + ")");
			}
			return materialized.array_elements[index];
		}
		if (auto materialized_row = tryMaterializeMultidimArrayRow(type_spec_opt, init_list, index, context)) {
			return *materialized_row;
		}
		const auto& elements = init_list.initializers();
		if (index >= elements.size()) {
			return EvalResult::error("Array index " + std::to_string(index) + " out of bounds (size " + std::to_string(elements.size()) + ")");
		}
		// Handle nested array row (multi-dimensional array element is an InitializerListNode).
		const ASTNode& elem = elements[index];
		if (elem.is<InitializerListNode>()) {
			if (element_is_struct_object) {
				if (const StructTypeInfo* struct_info = tryGetStructTypeInfo(element_type_index)) {
					return materialize_aggregate_object_value(
						struct_info,
						element_type_index,
						elem.as<InitializerListNode>(),
						context);
				}
			}
			return materialize_array_value(element_type_index, elem.as<InitializerListNode>(), context, nullptr);
		}
		return evaluate(elem, context);
	};

	if (!context.symbols) {
		return EvalResult::error("Cannot evaluate array subscript: no symbol table provided");
	}

	if (auto static_member_result = resolve_current_struct_static_member(
			&identifier,
			context,
			CurrentStructStaticLookupMode::PreferCurrentStruct);
		static_member_result.static_member) {
		TypeSpecifierNode static_member_type_spec = makeArrayTypeSpec(
			static_member_result.static_member->type_index,
			static_member_result.static_member->array_dimensions);
		bool element_is_struct_object =
			static_member_result.static_member->array_dimensions.size() == 1 &&
			tryGetStructTypeInfo(static_member_result.static_member->type_index) != nullptr;
		if (auto static_result = evaluate_array_initializer(
				static_member_result.static_member->initializer,
				static_member_result.static_member->type_index,
				element_is_struct_object,
				&static_member_type_spec)) {
			return *static_result;
		}

		StringHandle qualified_handle = StringTable::getOrInternStringHandle(
			StringBuilder().append(static_member_result.owner_struct->getName()).append("::"sv).append(identifier.nameHandle()).commit());
		auto qualified_symbol = context.symbols->lookup(qualified_handle);
		if (qualified_symbol.has_value() && qualified_symbol->is<VariableDeclarationNode>()) {
			const VariableDeclarationNode& qualified_var = qualified_symbol->as<VariableDeclarationNode>();
			if (!qualified_var.is_constexpr()) {
				return EvalResult::error("Static member array in array subscript must be constexpr");
			}
			TypeIndex qualified_element_type{};
			bool qualified_element_is_struct_object = false;
			const TypeSpecifierNode* qualified_type_spec = nullptr;
			{
				qualified_type_spec = &qualified_var.declaration().type_specifier_node();
				qualified_element_type = qualified_type_spec->type_index();
				qualified_element_is_struct_object =
					qualified_var.declaration().array_dimensions().size() == 1 &&
					tryGetStructTypeInfo(qualified_element_type) != nullptr;
			}
			if (auto qualified_result = evaluate_array_initializer(
					qualified_var.initializer(),
					qualified_element_type,
					qualified_element_is_struct_object,
					qualified_type_spec)) {
				return *qualified_result;
			}
		}

		return EvalResult::error("Static member array has no usable initializer in array subscript: " + std::string(var_name));
	}

	std::optional<ASTNode> symbol_opt = lookup_identifier_symbol(&identifier, var_name, *context.symbols);
	if (!symbol_opt.has_value()) {
		return EvalResult::error("Undefined variable in array subscript: " + std::string(var_name));
	}

	const ASTNode& symbol_node = symbol_opt.value();
	if (!symbol_node.is<VariableDeclarationNode>()) {
		return EvalResult::error("Identifier in array subscript is not a variable");
	}

	const VariableDeclarationNode& var_decl = symbol_node.as<VariableDeclarationNode>();
	if (!var_decl.is_constexpr()) {
		return EvalResult::error("Variable in array subscript must be constexpr");
	}

	const auto& initializer = var_decl.initializer();
	if (!initializer.has_value()) {
		return EvalResult::error("Constexpr array has no initializer");
	}

	if (initializer->is<InitializerListNode>()) {
		const TypeSpecifierNode& type_spec = var_decl.declaration().type_specifier_node();
		bool element_is_struct_object =
			var_decl.declaration().array_dimensions().size() == 1 &&
			tryGetStructTypeInfo(type_spec.type_index()) != nullptr;
		if (auto materialized_result = evaluate_array_initializer(
				initializer,
				type_spec.type_index(),
				element_is_struct_object,
				&type_spec)) {
			return *materialized_result;
		}
	}

	return EvalResult::error("Array variable is not initialized with an array initializer");
}

// Helper functions for type checking
bool Evaluator::isArithmeticType(TypeCategory type) {
	return ::isArithmeticType(type);
}

bool Evaluator::isFundamentalType(TypeCategory type) {
	return ::isFundamentalType(type);
}

// Evaluate type trait expressions (e.g., __is_void(int), __is_constant_evaluated())
EvalResult Evaluator::evaluate_type_trait(const TypeTraitExprNode& trait_expr) {
	// Handle __is_constant_evaluated() specially - it returns true during constexpr evaluation
	if (trait_expr.kind() == TypeTraitKind::IsConstantEvaluated) {
		// When evaluated in constexpr context, this always returns true
		return EvalResult::from_bool(true);
	}

	if (trait_expr.kind() == TypeTraitKind::IsSame &&
		trait_expr.has_second_type()) {
		TypeTraitResult trait_result = evaluateTypeTrait(trait_expr);
		if (trait_result.success) {
			return EvalResult::from_bool(trait_result.value);
		}
		return EvalResult::error("Failed to evaluate __is_same");
	}
	if (trait_expr.kind() == TypeTraitKind::IsSame) {
		return EvalResult::error("malformed __is_same: missing second type");
	}

	// For other type traits, we need to evaluate them based on the type
	// Most type traits can be evaluated at compile time
	if (!trait_expr.has_type()) {
		return EvalResult::error("Type trait requires a type argument");
	}

	const ASTNode& type_node = trait_expr.type_node();
	if (!type_node.is<TypeSpecifierNode>()) {
		return EvalResult::error("Type trait argument must be a type");
	}

	const TypeSpecifierNode& type_spec = type_node.as<TypeSpecifierNode>();
	TypeCategory type_cat = type_spec.category();
	CVQualifier type_cv = type_spec.cv_qualifier();
	bool is_reference = type_spec.is_reference();
	bool is_rvalue_reference = type_spec.is_rvalue_reference();
	size_t pointer_depth = type_spec.pointer_depth();
	bool is_array = type_spec.is_array();
	std::optional<size_t> array_size = type_spec.array_size();
	const ResolvedAliasTypeInfo resolved_trait_type =
		resolveAliasTypeInfo(type_spec.type_index());
	if (resolved_trait_type.terminal_type_info != nullptr &&
		(type_cat == TypeCategory::UserDefined ||
		 type_cat == TypeCategory::TypeAlias ||
		 type_cat == TypeCategory::Template ||
		 type_cat == TypeCategory::Invalid)) {
		type_cat = resolved_trait_type.typeEnum();
		type_cv |= resolved_trait_type.cv_qualifier;
		if (resolved_trait_type.reference_qualifier != ReferenceQualifier::None) {
			is_reference = true;
			is_rvalue_reference =
				resolved_trait_type.reference_qualifier == ReferenceQualifier::RValueReference;
		}
		pointer_depth += resolved_trait_type.pointer_depth;
		if (!is_array && resolved_trait_type.isArray()) {
			is_array = true;
			if (!resolved_trait_type.array_dimensions.empty()) {
				array_size = resolved_trait_type.array_dimensions.front();
			}
		}
	}

	bool result = false;

	// Evaluate the type trait based on its kind
	switch (trait_expr.kind()) {
	case TypeTraitKind::IsVoid:
		result = (type_cat == TypeCategory::Void && !is_reference && pointer_depth == 0);
		break;

	case TypeTraitKind::IsIntegral:
		result = (type_cat == TypeCategory::Bool ||
				  type_cat == TypeCategory::Char ||
				  type_cat == TypeCategory::Short ||
				  type_cat == TypeCategory::Int ||
				  type_cat == TypeCategory::Long ||
				  type_cat == TypeCategory::LongLong ||
				  type_cat == TypeCategory::UnsignedChar ||
				  type_cat == TypeCategory::UnsignedShort ||
				  type_cat == TypeCategory::UnsignedInt ||
				  type_cat == TypeCategory::UnsignedLong || type_cat == TypeCategory::UnsignedLongLong) &&
				 !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsFloatingPoint:
		result = (type_cat == TypeCategory::Float || type_cat == TypeCategory::Double || type_cat == TypeCategory::LongDouble) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsPointer:
		result = (pointer_depth > 0) && !is_reference;
		break;

	case TypeTraitKind::IsLvalueReference:
		result = is_reference && !is_rvalue_reference;
		break;

	case TypeTraitKind::IsRvalueReference:
		result = is_rvalue_reference;
		break;

	case TypeTraitKind::IsArray:
		result = is_array && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsReference:
		result = is_reference | is_rvalue_reference;
		break;

	case TypeTraitKind::IsArithmetic:
		result = isArithmeticType(type_cat) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsFundamental:
		result = isFundamentalType(type_cat) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsObject:
		result = (type_cat != TypeCategory::Function) & (type_cat != TypeCategory::Void) & !is_reference & !is_rvalue_reference;
		break;

	case TypeTraitKind::IsScalar:
		result = (isArithmeticType(type_cat) ||
				  type_cat == TypeCategory::Enum || type_cat == TypeCategory::Nullptr ||
				  type_cat == TypeCategory::MemberObjectPointer || type_cat == TypeCategory::MemberFunctionPointer ||
				  pointer_depth > 0) &&
				 !is_reference;
		break;

	case TypeTraitKind::IsCompound:
		result = !(isFundamentalType(type_cat) & !is_reference & (pointer_depth == 0));
		break;

	case TypeTraitKind::IsConst:
		result = hasCVQualifier(type_cv, CVQualifier::Const);
		break;

	case TypeTraitKind::IsVolatile:
		result = hasCVQualifier(type_cv, CVQualifier::Volatile);
		break;

	case TypeTraitKind::IsSigned:
		result = is_signed_integer_type(type_cat) && !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsUnsigned:
		result = (type_cat == TypeCategory::Bool || is_unsigned_integer_type(type_cat)) &&
				 !is_reference && pointer_depth == 0;
		break;

	case TypeTraitKind::IsBoundedArray:
		result = is_array & int(array_size.value_or(0) > 0) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsUnboundedArray:
		result = is_array & int(array_size.value_or(0) == 0) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsAggregate:
			// Arrays are aggregates
		result = is_array & !is_reference & (pointer_depth == 0);
			// For struct types, we need runtime type info, so fall through to default
		break;

	case TypeTraitKind::IsEnum:
		result = (type_cat == TypeCategory::Enum) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsNullptr:
		result = (type_cat == TypeCategory::Nullptr) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsMemberObjectPointer:
		result = (type_cat == TypeCategory::MemberObjectPointer) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsMemberFunctionPointer:
		result = (type_cat == TypeCategory::MemberFunctionPointer) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsFunction:
		result = (type_cat == TypeCategory::Function) & !is_reference & (pointer_depth == 0);
		break;

	case TypeTraitKind::IsUnion:
	case TypeTraitKind::IsClass:
		{
			// IsUnion and IsClass require struct_info to distinguish union vs non-union structs;
			// delegate to the TypeTraitEvaluator which has access to struct metadata.
			TypeTraitResult trait_result = evaluateTypeTrait(trait_expr);
			if (trait_result.success) {
				return EvalResult::from_bool(trait_result.value);
			}
			return EvalResult::error("Failed to evaluate __is_union / __is_class");
		}

	case TypeTraitKind::IsCompleteOrUnbounded:
		{
			const StructTypeInfo* struct_info = nullptr;
			if (resolved_trait_type.terminal_type_info != nullptr) {
				struct_info = resolved_trait_type.terminal_type_info->getStructInfo();
			} else if (type_spec.type_index().is_valid() && is_struct_type(type_cat)) {
				struct_info = getTypeInfo(type_spec.type_index()).getStructInfo();
			}
			TypeTraitResult trait_result = evaluateTypeTrait(
				TypeTraitKind::IsCompleteOrUnbounded,
				type_spec,
				struct_info);
			if (trait_result.success) {
				return EvalResult::from_bool(trait_result.value);
			}
			return EvalResult::error("Failed to evaluate __is_complete_or_unbounded");
		}

	case TypeTraitKind::IsFinal:
	case TypeTraitKind::IsPolymorphic:
	case TypeTraitKind::IsAbstract:
	case TypeTraitKind::IsEmpty:
	case TypeTraitKind::IsStandardLayout:
	case TypeTraitKind::IsTriviallyCopyable:
	case TypeTraitKind::IsTrivial:
	case TypeTraitKind::IsPod:
	case TypeTraitKind::IsConstructible:
	case TypeTraitKind::IsTriviallyConstructible:
	case TypeTraitKind::IsNothrowConstructible:
		{
			// Delegate to the variadic evaluateTypeTrait overload so additional type arguments
			// (the constructor argument types) are taken into account.
			TypeTraitResult trait_result = evaluateTypeTrait(trait_expr);
			if (trait_result.success) {
				return EvalResult::from_bool(trait_result.value);
			}
			return EvalResult::error("Failed to evaluate type trait");
		}

	case TypeTraitKind::IsDestructible:
	case TypeTraitKind::IsTriviallyDestructible:
	case TypeTraitKind::IsNothrowDestructible:
	case TypeTraitKind::HasTrivialDestructor:
		{
			const StructTypeInfo* struct_info = nullptr;
			if (resolved_trait_type.terminal_type_info != nullptr) {
				struct_info = resolved_trait_type.terminal_type_info->getStructInfo();
			} else if (type_spec.type_index().is_valid() && is_struct_type(type_cat)) {
				struct_info = getTypeInfo(type_spec.type_index()).getStructInfo();
			}
			TypeTraitResult trait_result = evaluateTypeTrait(trait_expr.kind(), type_spec, struct_info);
			if (trait_result.success) {
				return EvalResult::from_bool(trait_result.value);
			}
			return EvalResult::error("Failed to evaluate type trait");
		}

	default:
		result = false;
		break;
	}

	return EvalResult::from_bool(result);
}

} // namespace ConstExpr
