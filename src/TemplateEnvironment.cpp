#include "TemplateEnvironment.h"

#include "TemplateRegistry_Pattern.h"
#include "TemplateRegistry_Types.h"

#include <algorithm>
#include <unordered_set>

const TemplateTypeArg* TemplateEnvironment::findOne(StringHandle name) const {
	for (const TemplateBinding& binding : bindings) {
		if (binding.name != name || binding.is_pack || binding.args.empty()) {
			continue;
		}
		return &binding.args.front();
	}
	return parent != nullptr ? parent->findOne(name) : nullptr;
}

std::span<const TemplateTypeArg> TemplateEnvironment::findPack(StringHandle name) const {
	for (const TemplateBinding& binding : bindings) {
		if (binding.name != name || !binding.is_pack) {
			continue;
		}
		return std::span<const TemplateTypeArg>(binding.args.data(), binding.args.size());
	}
	return parent != nullptr ? parent->findPack(name) : std::span<const TemplateTypeArg>{};
}

TypeInfo::TemplateArgInfo toTemplateArgInfo(const TemplateTypeArg& arg) {
	TypeInfo::TemplateArgInfo info;
	info.type_index = arg.type_index; // carries both index and TypeCategory
	info.pointer_depth = arg.pointer_depth;
	info.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	info.ref_qualifier = arg.ref_qualifier;
	info.cv_qualifier = arg.cv_qualifier;
	info.is_array = arg.is_array;
	info.array_size = arg.array_size();
	info.value = arg.value;
	info.is_value = arg.is_value;
	info.dependent_name = arg.dependent_name;
	info.function_signature = arg.function_signature;
	info.dependent_expr = arg.dependent_expr;
	return info;
}

TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg) {
	TemplateTypeArg ta;
	ta.type_index = arg.type_index;
	ta.is_value = arg.is_value;
	ta.cv_qualifier = arg.cv_qualifier;
	ta.ref_qualifier = arg.ref_qualifier;
	ta.pointer_depth = static_cast<uint8_t>(arg.pointer_depth);
	ta.is_array = arg.is_array;
	ta.array_dimensions = arg.array_size.has_value()
		? std::vector<size_t>{*arg.array_size}
		: std::vector<size_t>{};
	ta.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	ta.dependent_name = arg.dependent_name;
	ta.dependent_expr = arg.dependent_expr;
	ta.function_signature = arg.function_signature;
	ta.is_dependent = arg.dependent_name.isValid() || arg.dependent_expr.has_value();
	if (arg.is_value) {
		ta.value = arg.intValue();
	}
	return ta;
}

InlineVector<TypeInfo::TemplateArgInfo, 4> toTemplateArgInfoList(std::span<const TemplateTypeArg> args) {
	InlineVector<TypeInfo::TemplateArgInfo, 4> result;
	result.reserve(args.size());
	for (const TemplateTypeArg& arg : args) {
		result.push_back(toTemplateArgInfo(arg));
	}
	return result;
}

InlineVector<TemplateTypeArg, 4> toTemplateTypeArgList(std::span<const TypeInfo::TemplateArgInfo> args) {
	InlineVector<TemplateTypeArg, 4> result;
	result.reserve(args.size());
	for (const TypeInfo::TemplateArgInfo& arg : args) {
		result.push_back(toTemplateTypeArg(arg));
	}
	return result;
}

namespace {

size_t countRequiredTemplateArgsAfter(
	std::span<const TemplateParameterNode> params,
	size_t start_index) {
	size_t count = 0;
	for (size_t i = start_index; i < params.size(); ++i) {
		if (!params[i].is_variadic()) {
			++count;
		}
	}
	return count;
}

TemplateParameterKind inferBindingKind(const TemplateTypeArg& arg) {
	if (arg.is_template_template_arg) {
		return TemplateParameterKind::Template;
	}
	if (arg.is_value) {
		return TemplateParameterKind::NonType;
	}
	return TemplateParameterKind::Type;
}

void appendContextBindings(
	const TypeInfo::InstantiationContext* context,
	InlineVector<TemplateBinding, 4>& out_bindings) {
	if (context == nullptr) {
		return;
	}
	appendContextBindings(context->parent, out_bindings);
	const size_t binding_count = std::min(context->param_names.size(), context->param_args.size());
	for (size_t i = 0; i < binding_count; ++i) {
		TemplateBinding binding;
		binding.name = context->param_names[i];
		TemplateTypeArg arg = toTemplateTypeArg(context->param_args[i]);
		binding.kind = inferBindingKind(arg);
		binding.args.push_back(std::move(arg));
		out_bindings.push_back(std::move(binding));
	}
}

} // namespace

TemplateEnvironment buildTemplateEnvironment(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent) {
	TemplateEnvironment environment;
	environment.parent = parent;
	environment.bindings.reserve(params.size());

	size_t arg_index = 0;
	for (size_t i = 0; i < params.size(); ++i) {
		const TemplateParameterNode& param = params[i];
		TemplateBinding binding;
		binding.name = param.nameHandle();
		binding.kind = param.kind();
		binding.is_pack = param.is_variadic();

		if (binding.is_pack) {
			const size_t remaining_args = arg_index < args.size() ? args.size() - arg_index : 0;
			const size_t required_after = countRequiredTemplateArgsAfter(params, i + 1);
			const size_t pack_size = remaining_args > required_after ? remaining_args - required_after : 0;
			binding.args.reserve(pack_size);
			for (size_t pack_index = 0; pack_index < pack_size && (arg_index + pack_index) < args.size(); ++pack_index) {
				binding.args.push_back(args[arg_index + pack_index]);
			}
			arg_index += pack_size;
			environment.bindings.push_back(std::move(binding));
			continue;
		}

		if (arg_index < args.size()) {
			binding.args.push_back(args[arg_index]);
			++arg_index;
		}
		environment.bindings.push_back(std::move(binding));
	}

	return environment;
}

TemplateEnvironment buildTemplateEnvironment(const OuterTemplateBinding& binding) {
	InlineVector<TemplateParameterNode, 4> params;
	params.reserve(binding.params.size());
	for (const ASTNode& param_node : binding.params) {
		if (!param_node.is<TemplateParameterNode>()) {
			continue;
		}
		params.push_back(param_node.as<TemplateParameterNode>());
	}
	if (!params.empty() && params.size() == binding.param_names.size()) {
		return buildTemplateEnvironment(
			std::span<const TemplateParameterNode>(params.data(), params.size()),
			std::span<const TemplateTypeArg>(binding.all_args.data(), binding.all_args.size()),
			nullptr);
	}

	TemplateEnvironment environment;
	const size_t pair_count = std::min(binding.param_names.size(), binding.param_args.size());
	environment.bindings.reserve(pair_count);
	for (size_t i = 0; i < pair_count; ++i) {
		TemplateBinding entry;
		entry.name = binding.param_names[i];
		entry.kind = inferBindingKind(binding.param_args[i]);
		entry.args.push_back(binding.param_args[i]);
		environment.bindings.push_back(std::move(entry));
	}
	return environment;
}

TemplateEnvironment buildTemplateEnvironment(const TypeInfo::InstantiationContext& context) {
	TemplateEnvironment environment;
	appendContextBindings(&context, environment.bindings);
	return environment;
}

std::optional<TemplateTypeArg> resolveContextBinding(
	StringHandle target_name,
	const TemplateEnvironment& environment) {
	std::unordered_set<StringHandle, StringHandleHash> resolution_stack;
	auto resolve_binding = [&](auto&& self, StringHandle current_name) -> std::optional<TemplateTypeArg> {
		if (!current_name.isValid()) {
			return std::nullopt;
		}
		if (!resolution_stack.insert(current_name).second) {
			return std::nullopt;
		}

		const TemplateTypeArg* bound_arg = environment.findOne(current_name);
		if (bound_arg == nullptr ||
			bound_arg->is_value ||
			bound_arg->is_template_template_arg) {
			resolution_stack.erase(current_name);
			return std::nullopt;
		}
		if (!bound_arg->is_dependent) {
			TemplateTypeArg resolved = *bound_arg;
			resolution_stack.erase(current_name);
			return resolved;
		}
		if (!bound_arg->dependent_name.isValid() ||
			bound_arg->dependent_name == current_name) {
			resolution_stack.erase(current_name);
			return std::nullopt;
		}

		auto rebound = self(self, bound_arg->dependent_name);
		if (!rebound.has_value()) {
			resolution_stack.erase(current_name);
			return std::nullopt;
		}
		TemplateTypeArg resolved = rebindDependentTemplateTypeArg(*rebound, *bound_arg);
		resolution_stack.erase(current_name);
		return resolved;
	};

	return resolve_binding(resolve_binding, target_name);
}

