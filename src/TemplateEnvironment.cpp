#include "TemplateEnvironment.h"

#include "ChunkedAnyVector.h"
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
	info.array_dimensions.assign(arg.array_dimensions.begin(), arg.array_dimensions.end());
	info.is_value = arg.is_value;
	info.is_pack = arg.is_pack;
	info.dependent_name = arg.dependent_name;
	info.function_signature = arg.function_signature;
	info.dependent_expr = arg.dependent_expr;
	info.is_template_template_arg = arg.is_template_template_arg;
	info.template_name = arg.template_name_handle;
	info.member_pointer_kind = arg.member_pointer_kind;
	// Default: store the integer value. For pointer/reference NTTPs with a named entity,
	// override with the entity_name StringHandle so the identity survives the roundtrip.
	info.value = arg.value;
	if (arg.has_typed_value_identity) {
		const auto& id = arg.typed_value_identity;
		if (id.entity_name.isValid() &&
			(id.kind == FlashCpp::NonTypeValueIdentityKind::ObjectPointer ||
			 id.kind == FlashCpp::NonTypeValueIdentityKind::FunctionPointer ||
			 id.kind == FlashCpp::NonTypeValueIdentityKind::Reference)) {
			info.value = id.entity_name;
		}
	}
	return info;
}

TemplateTypeArg toTemplateTypeArg(const TypeInfo::TemplateArgInfo& arg) {
	TemplateTypeArg ta;
	ta.type_index = arg.type_index;
	ta.is_value = arg.is_value;
	ta.is_pack = arg.is_pack;
	ta.cv_qualifier = arg.cv_qualifier;
	ta.ref_qualifier = arg.ref_qualifier;
	ta.pointer_depth = static_cast<uint8_t>(arg.pointer_depth);
	ta.is_array = arg.is_array;
	ta.array_dimensions.assign(arg.array_dimensions.begin(), arg.array_dimensions.end());
	ta.pointer_cv_qualifiers = arg.pointer_cv_qualifiers;
	ta.dependent_name = arg.dependent_name;
	ta.dependent_expr = arg.dependent_expr;
	ta.function_signature = arg.function_signature;
	ta.is_dependent = arg.dependent_name.isValid() || arg.dependent_expr.has_value();
	ta.is_template_template_arg = arg.is_template_template_arg;
	ta.template_name_handle = arg.template_name;
	ta.member_pointer_kind = arg.member_pointer_kind;
	if (arg.is_value) {
		// If entity_name was stored as a StringHandle, reconstruct the typed_value_identity.
		// The kind is inferred from stored fields rather than from an explicit kind tag:
		//   - TypeCategory::FunctionPointer/MemberFunctionPointer → FunctionPointer kind
		//   - ref_qualifier != None → Reference kind
		//   - otherwise → ObjectPointer kind
		// This mirrors the categorisation applied during serialisation in toTemplateArgInfo.
		if (StringHandle sh = arg.stringValue(); sh.isValid()) {
			TypeCategory cat = arg.category();
			if (cat == TypeCategory::FunctionPointer || cat == TypeCategory::MemberFunctionPointer) {
				ta.setValueIdentity(FlashCpp::NonTypeValueIdentity::makeFunctionPointer(arg.type_index, sh));
			} else if (arg.ref_qualifier != ReferenceQualifier::None) {
				ta.setValueIdentity(FlashCpp::NonTypeValueIdentity::makeReference(arg.type_index, sh));
			} else {
				ta.setValueIdentity(FlashCpp::NonTypeValueIdentity::makeObjectPointer(arg.type_index, sh, 0));
			}
		} else {
			ta.value = arg.intValue();
		}
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

TemplateParameterKind inferBindingKind(const TypeInfo::TemplateArgInfo& arg) {
	if (arg.is_template_template_arg) {
		return TemplateParameterKind::Template;
	}
	if (arg.is_value) {
		return TemplateParameterKind::NonType;
	}
	return TemplateParameterKind::Type;
}

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
	if (context->hasInstantiationContextBindings()) {
		for (const TypeInfo::InstantiationContext::Binding& context_binding : context->bindings) {
			TemplateBinding binding;
			binding.name = context_binding.name;
			binding.kind = static_cast<TemplateParameterKind>(context_binding.kind);
			binding.is_pack = context_binding.is_pack;
			binding.args.reserve(context_binding.args.size());
			for (const TypeInfo::TemplateArgInfo& arg_info : context_binding.args) {
				binding.args.push_back(toTemplateTypeArg(arg_info));
			}
			binding.is_pack |= (binding.args.size() > 1);
			out_bindings.push_back(std::move(binding));
		}
		appendContextBindings(context->parent, out_bindings);
		return;
	}

	const size_t binding_count = std::min(context->param_names.size(), context->param_args.size());
	for (size_t i = 0; i < binding_count;) {
		TemplateBinding binding;
		binding.name = context->param_names[i];
		TemplateTypeArg arg = toTemplateTypeArg(context->param_args[i]);
		binding.kind = inferBindingKind(arg);
		binding.is_pack = arg.is_pack;
		binding.args.push_back(std::move(arg));

		size_t next_index = i + 1;
		while (next_index < binding_count && context->param_names[next_index] == binding.name) {
			binding.is_pack = true;
			binding.args.push_back(toTemplateTypeArg(context->param_args[next_index]));
			++next_index;
		}
		out_bindings.push_back(std::move(binding));
		i = next_index;
	}
	appendContextBindings(context->parent, out_bindings);
}

// Build only the bindings introduced by the current scope. Parent bindings stay
// in their own snapshot node so nested snapshots can share the parent chain.
InlineVector<TemplateBindingSnapshot, 4> buildSnapshotBindingsForCurrentScope(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args) {
	InlineVector<TemplateBindingSnapshot, 4> bindings;
	const size_t pair_count = std::min(param_names.size(), args.size());
	bindings.reserve(pair_count);
	for (size_t i = 0; i < pair_count; ++i) {
		if (!bindings.empty() &&
			bindings.back().name == param_names[i]) {
			TemplateBindingSnapshot& prior = bindings.back();
			prior.is_pack = true;
			prior.args.push_back(args[i]);
			continue;
		}
		TemplateBindingSnapshot binding;
		binding.name = param_names[i];
		binding.kind = inferBindingKind(args[i]);
		binding.is_pack = args[i].is_pack;
		binding.args.push_back(args[i]);
		bindings.push_back(std::move(binding));
	}
	return bindings;
}

template <typename Fn>
void forEachTemplateEnvironmentSnapshotBinding(
	const TemplateEnvironmentSnapshotNode* node,
	Fn&& fn) {
	// Walk parent-first so legacy replay and environment reconstruction see the
	// same outer-to-inner binding order that flattened snapshots previously had.
	InlineVector<const TemplateEnvironmentSnapshotNode*, 4> chain;
	for (const TemplateEnvironmentSnapshotNode* current = node;
		current != nullptr;
		current = current->parent) {
		chain.push_back(current);
	}
	for (size_t i = chain.size(); i > 0; --i) {
		for (const TemplateBindingSnapshot& binding : chain[i - 1]->bindings) {
			fn(binding);
		}
	}
}

size_t countTemplateEnvironmentSnapshotBindings(
	const TemplateEnvironmentSnapshotNode* node) {
	// Count one entry per stored binding segment across the full parent chain.
	size_t count = 0;
	for (const TemplateEnvironmentSnapshotNode* current = node;
		current != nullptr;
		current = current->parent) {
		count += current->bindings.size();
	}
	return count;
}

size_t countTemplateEnvironmentSnapshotLegacyEntries(
	const TemplateEnvironmentSnapshotNode* node) {
	// Legacy param/arg views expand packs back into one entry per argument, so
	// reserve using that expanded count across the full parent chain.
	size_t count = 0;
	for (const TemplateEnvironmentSnapshotNode* current = node;
		current != nullptr;
		current = current->parent) {
		for (const TemplateBindingSnapshot& binding : current->bindings) {
			count += binding.is_pack
				? binding.args.size()
				: static_cast<size_t>(!binding.args.empty());
		}
	}
	return count;
}

} // namespace

TemplateEnvironmentSnapshot buildTemplateEnvironmentSnapshot(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args,
	const TemplateEnvironmentSnapshot* parent) {
	TemplateEnvironmentSnapshot snapshot;
	// An empty snapshot is represented by a null node pointer.
	InlineVector<TemplateBindingSnapshot, 4> bindings =
		buildSnapshotBindingsForCurrentScope(param_names, args);
	if (bindings.empty()) {
		// Keep empty snapshots pointer-free, but preserve an existing parent chain
		// when this scope introduces no new bindings.
		if (parent != nullptr) {
			snapshot = *parent;
		}
		return snapshot;
	}
	TemplateEnvironmentSnapshotNode& node =
		gChunkedAnyStorage.emplace_back<TemplateEnvironmentSnapshotNode>();
	node.parent = parent != nullptr ? parent->node : nullptr;
	node.bindings = std::move(bindings);
	snapshot.node = &node;
	return snapshot;
}

TemplateEnvironmentSnapshot buildTemplateEnvironmentSnapshot(
	std::span<const StringHandle> param_names,
	std::span<const TypeInfo::TemplateArgInfo> args) {
	return buildTemplateEnvironmentSnapshot(param_names, args, nullptr);
}

bool hasTemplateEnvironmentSnapshotBindings(const TemplateEnvironmentSnapshot& snapshot) {
	return snapshot.node != nullptr;
}

void populateTemplateEnvironmentLegacyViews(
	const TemplateEnvironmentSnapshot& snapshot,
	InlineVector<StringHandle, 4>& out_param_names,
	InlineVector<TypeInfo::TemplateArgInfo, 4>& out_args) {
	out_param_names.clear();
	out_args.clear();
	const size_t entry_count = countTemplateEnvironmentSnapshotLegacyEntries(snapshot.node);
	out_param_names.reserve(entry_count);
	out_args.reserve(entry_count);
	forEachTemplateEnvironmentSnapshotBinding(
		snapshot.node,
		[&](const TemplateBindingSnapshot& binding) {
			if (binding.is_pack) {
				for (const auto& arg : binding.args) {
					out_param_names.push_back(binding.name);
					out_args.push_back(arg);
				}
				return;
			}

			if (binding.args.empty()) {
				return;
			}
			out_param_names.push_back(binding.name);
			out_args.push_back(binding.args.front());
		});
}

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

TemplateEnvironment buildTemplateEnvironment(const TemplateEnvironmentSnapshot& snapshot) {
	TemplateEnvironment environment;
	environment.bindings.reserve(countTemplateEnvironmentSnapshotBindings(snapshot.node));
	forEachTemplateEnvironmentSnapshotBinding(
		snapshot.node,
		[&](const TemplateBindingSnapshot& snapshot_binding) {
			TemplateBinding binding;
			binding.name = snapshot_binding.name;
			binding.kind = snapshot_binding.kind;
			binding.is_pack = snapshot_binding.is_pack;
			binding.args.reserve(snapshot_binding.args.size());
			for (const TypeInfo::TemplateArgInfo& snapshot_arg : snapshot_binding.args) {
				binding.args.push_back(toTemplateTypeArg(snapshot_arg));
			}
			environment.bindings.push_back(std::move(binding));
		});
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
	if (!params.empty()) {
		return buildTemplateEnvironment(
			std::span<const TemplateParameterNode>(params.data(), params.size()),
			std::span<const TemplateTypeArg>(binding.all_args.data(), binding.all_args.size()),
			nullptr);
	}

	TemplateEnvironment environment;
	if (binding.param_names.size() != binding.param_args.size()) {
		if (binding.all_args.empty() || binding.param_names.empty()) {
			return environment;
		}
	}

	const std::span<const TemplateTypeArg> source_args =
		!binding.all_args.empty()
			? std::span<const TemplateTypeArg>(binding.all_args.data(), binding.all_args.size())
			: std::span<const TemplateTypeArg>(binding.param_args.data(), binding.param_args.size());
	const size_t pair_count = std::min(binding.param_names.size(), source_args.size());
	environment.bindings.reserve(pair_count);
	for (size_t i = 0; i < pair_count;) {
		TemplateBinding entry;
		entry.name = binding.param_names[i];
		entry.kind = inferBindingKind(source_args[i]);
		entry.args.push_back(source_args[i]);

		size_t next_index = i + 1;
		while (next_index < pair_count && binding.param_names[next_index] == entry.name) {
			entry.is_pack = true;
			entry.args.push_back(source_args[next_index]);
			++next_index;
		}
		if (next_index == pair_count && source_args.size() > pair_count) {
			entry.is_pack = true;
			for (size_t extra = pair_count; extra < source_args.size(); ++extra) {
				entry.args.push_back(source_args[extra]);
			}
		}
		environment.bindings.push_back(std::move(entry));
		i = next_index;
	}
	return environment;
}

TemplateEnvironment buildTemplateEnvironment(const TypeInfo::InstantiationContext& context) {
	TemplateEnvironment environment;
	appendContextBindings(&context, environment.bindings);
	return environment;
}

TemplateInstantiationContext buildTemplateInstantiationContext(
	std::span<const TemplateParameterNode> params,
	std::span<const TemplateTypeArg> args,
	const TemplateEnvironment* parent,
	TemplateSubstitutionFailurePolicy failure_policy) {
	TemplateInstantiationContext context;
	context.template_parameters = params;
	context.template_arguments = args;
	context.environment = buildTemplateEnvironment(params, args, parent);
	context.failure_policy = failure_policy;

	for (const TemplateBinding& binding : context.environment.bindings) {
		if (!binding.is_pack) {
			continue;
		}
		TemplatePackExpansionState pack_state;
		pack_state.pack_name = binding.name;
		pack_state.element_count = binding.args.size();
		pack_state.expanding = true;
		context.pack_state.push_back(pack_state);
	}

	return context;
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
		if (!bound_arg->is_dependent &&
			bound_arg->type_index.is_valid() &&
			typeIndexContainsDependentPlaceholder(bound_arg->type_index)) {
			if (const TypeInfo* dependent_type_info = tryGetTypeInfo(bound_arg->type_index);
				dependent_type_info != nullptr &&
				dependent_type_info->name().isValid() &&
				dependent_type_info->name() != current_name) {
				auto rebound = self(self, dependent_type_info->name());
				if (rebound.has_value()) {
					TemplateTypeArg resolved = rebindDependentTemplateTypeArg(*rebound, *bound_arg);
					resolution_stack.erase(current_name);
					return resolved;
				}
			}
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
