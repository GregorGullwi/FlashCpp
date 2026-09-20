#include "CanonicalTypes.h"

// --- CanonicalTypeTransaction ---

void CanonicalTypeTransaction::commit() {
	if (depth_ != 0) {
		table_.finishTransaction(depth_, true);
		depth_ = 0;
	}
}

void CanonicalTypeTransaction::rollback() {
	if (depth_ != 0) {
		table_.finishTransaction(depth_, false);
		depth_ = 0;
	}
}

// --- CanonicalTypeTable ---

TypeId CanonicalTypeTable::builtin(CanonicalBuiltinKind kind) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (kind >= CanonicalBuiltinKind::Count) {
		throw InternalError("canonical type: invalid builtin kind");
	}
	return internUnlocked({
		.child = TypeId{},
		.kind = CanonicalTypeKind::Builtin,
		.builtin = kind,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = 0,
	});
}

TypeId CanonicalTypeTable::qualify(TypeId type, CVQualifier qualifiers) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	CanonicalTypeNode input = nodeUnlocked(type);
	if (static_cast<uint8_t>(qualifiers) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
		throw InternalError("canonical type: invalid cv qualifiers");
	}
	if (isInternalLink(input.kind)) {
		throw InternalError("canonical type: qualify function parameter link");
	}
	// [dcl.ref]: cv-qualification introduced through a reference typedef is
	// ignored. Referent qualification remains on the child node.
	if (qualifiers == CVQualifier::None || isReference(input.kind)) {
		return type;
	}
	// [dcl.fct]: cv-qualifiers on a function type are part of that type.
	if (input.kind == CanonicalTypeKind::Function) {
		input.qualifiers |= qualifiers;
		return internUnlocked(input);
	}
	std::vector<CanonicalTypeNode> arrays;
	while (input.kind == CanonicalTypeKind::Array) {
		arrays.push_back(input);
		type = input.child;
		input = nodeUnlocked(type);
	}
	if (input.kind == CanonicalTypeKind::Function) {
		input.qualifiers |= qualifiers;
		type = internUnlocked(input);
	} else {
		if (input.kind == CanonicalTypeKind::Qualified) {
			qualifiers |= input.qualifiers;
			type = input.child;
		}
		type = internUnlocked({
			.child = type,
			.kind = CanonicalTypeKind::Qualified,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = qualifiers,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = 0,
		});
	}
	for (auto array = arrays.rbegin(); array != arrays.rend(); ++array) {
		array->child = type;
		type = internUnlocked(*array);
	}
	return type;
}

TypeId CanonicalTypeTable::pointer(TypeId pointee) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto kind = nodeUnlocked(pointee).kind;
	if (isReference(kind) || isInternalLink(kind)) {
		throw InternalError("canonical type: invalid pointer pointee");
	}
	return internUnlocked({
		.child = pointee,
		.kind = CanonicalTypeKind::Pointer,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = 0,
	});
}

TypeId CanonicalTypeTable::array(TypeId element, size_t extent) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (extent == 0) {
		throw InternalError("canonical type: zero array bound");
	}
	return arrayUnlocked(element, static_cast<uint64_t>(extent), CanonicalTypeNodeFlags::KnownArrayBound);
}

TypeId CanonicalTypeTable::arrayOfUnknownBound(TypeId element) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return arrayUnlocked(element, 0, CanonicalTypeNodeFlags::None);
}

TypeId CanonicalTypeTable::reference(TypeId referent, ReferenceQualifier qualifier) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	CanonicalTypeNode input = nodeUnlocked(referent);
	if (isInternalLink(input.kind)) {
		throw InternalError("canonical type: reference to function parameter link");
	}
	if (qualifier != ReferenceQualifier::LValueReference && qualifier != ReferenceQualifier::RValueReference) {
		throw InternalError("canonical type: invalid reference qualifier");
	}
	auto kind = qualifier == ReferenceQualifier::LValueReference
		? CanonicalTypeKind::LValueReference : CanonicalTypeKind::RValueReference;
	if (isReference(input.kind)) {
		// [dcl.ref] reference collapsing: only && combined with && stays &&.
		if (input.kind == CanonicalTypeKind::LValueReference) {
			kind = CanonicalTypeKind::LValueReference;
		}
		referent = input.child;
		input = nodeUnlocked(referent);
	}
	const auto base = input.kind == CanonicalTypeKind::Qualified ? nodeUnlocked(input.child) : input;
	if (base.kind == CanonicalTypeKind::Builtin && base.builtin == CanonicalBuiltinKind::Void) {
		throw InternalError("canonical type: reference to void");
	}
	return internUnlocked({
		.child = referent,
		.kind = kind,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = 0,
	});
}

TypeId CanonicalTypeTable::function(TypeId return_type, std::span<const TypeId> parameters, bool is_variadic,
	CVQualifier function_cv, ReferenceQualifier function_ref, bool is_noexcept,
	CanonicalCallingConvention calling_convention, CanonicalDllLinkage dll_linkage,
	ExprId dependent_noexcept) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (static_cast<uint8_t>(function_cv) > static_cast<uint8_t>(CVQualifier::ConstVolatile)) {
		throw InternalError("canonical type: invalid function cv qualifiers");
	}
	if (function_ref != ReferenceQualifier::None &&
		function_ref != ReferenceQualifier::LValueReference &&
		function_ref != ReferenceQualifier::RValueReference) {
		throw InternalError("canonical type: invalid function ref qualifier");
	}
	if (calling_convention >= CanonicalCallingConvention::Count) {
		throw InternalError("canonical type: invalid calling convention");
	}
	if (is_noexcept && dependent_noexcept) {
		throw InternalError("canonical type: plain noexcept cannot combine with dependent noexcept");
	}
	const CanonicalTypeNode return_node = nodeUnlocked(return_type);
	if (return_node.kind == CanonicalTypeKind::Function ||
		isInternalLink(return_node.kind) ||
		return_node.kind == CanonicalTypeKind::Array) {
		throw InternalError("canonical type: invalid function return type");
	}
	CanonicalTypeNodeFlags flags = CanonicalTypeNodeFlags::None;
	if (is_variadic) {
		flags |= CanonicalTypeNodeFlags::VariadicFunction;
	}
	if (is_noexcept) {
		flags |= CanonicalTypeNodeFlags::NoexceptFunction;
	}
	if (dependent_noexcept) {
		flags |= CanonicalTypeNodeFlags::DependentNoexceptFunction;
	}
	if (function_ref == ReferenceQualifier::LValueReference) {
		flags |= CanonicalTypeNodeFlags::FunctionLValueRef;
	} else if (function_ref == ReferenceQualifier::RValueReference) {
		flags |= CanonicalTypeNodeFlags::FunctionRValueRef;
	}
	if (dll_linkage == CanonicalDllLinkage::Import) {
		flags |= CanonicalTypeNodeFlags::FunctionDllImport;
	} else if (dll_linkage == CanonicalDllLinkage::Export) {
		flags |= CanonicalTypeNodeFlags::FunctionDllExport;
	}
	TypeId param_link{};
	for (size_t index = parameters.size(); index-- > 0;) {
		const TypeId parameter = parameters[index];
		const CanonicalTypeNode parameter_node = nodeUnlocked(parameter);
		if (parameter_node.kind == CanonicalTypeKind::Function ||
			isInternalLink(parameter_node.kind) ||
			parameter_node.kind == CanonicalTypeKind::Array) {
			throw InternalError("canonical type: undecayed function parameter type");
		}
		param_link = internUnlocked({
			.child = param_link,
			.kind = CanonicalTypeKind::FunctionParam,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = parameter.value,
		});
	}
	return internUnlocked({
		.child = return_type,
		.kind = CanonicalTypeKind::Function,
		.builtin = static_cast<CanonicalBuiltinKind>(calling_convention),
		.qualifiers = function_cv,
		.flags = flags,
		.array_extent = packFunctionArrayExtent(param_link, dependent_noexcept),
	});
}

TypeId CanonicalTypeTable::record(EntityId entity) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!entity) {
		throw InternalError("canonical type: invalid record EntityId");
	}
	return internUnlocked({
		.child = TypeId{},
		.kind = CanonicalTypeKind::Record,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = entity.value,
	});
}

TypeId CanonicalTypeTable::enumeration(EntityId entity) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!entity) {
		throw InternalError("canonical type: invalid enum EntityId");
	}
	return internUnlocked({
		.child = TypeId{},
		.kind = CanonicalTypeKind::Enum,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = entity.value,
	});
}

TypeId CanonicalTypeTable::templateParameter(TemplateDeclId template_decl, uint32_t parameter_index) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!template_decl) {
		throw InternalError("canonical type: invalid template-parameter TemplateDeclId");
	}
	return internUnlocked({
		.child = TypeId{},
		.kind = CanonicalTypeKind::TemplateParameter,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = packTemplateParameterExtent(template_decl, parameter_index),
	});
}

TypeId CanonicalTypeTable::templateSpecialization(TemplateDeclId primary,
	std::span<const CanonicalTemplateArgument> arguments) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!primary) {
		throw InternalError("canonical type: invalid specialization TemplateDeclId");
	}
	return internUnlocked({
		.child = rebuildMixedTemplateArgListUnlocked(arguments),
		.kind = CanonicalTypeKind::TemplateSpecialization,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = primary.value,
	});
}

TypeId CanonicalTypeTable::aliasTemplateSpecialization(TemplateDeclId primary,
	std::span<const CanonicalTemplateArgument> arguments) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!primary) {
		throw InternalError("canonical type: invalid alias specialization TemplateDeclId");
	}
	return internUnlocked({
		.child = rebuildMixedTemplateArgListUnlocked(arguments),
		.kind = CanonicalTypeKind::AliasTemplateSpecialization,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = primary.value,
	});
}

TypeId CanonicalTypeTable::aliasTemplateSpecialization(TemplateDeclId primary, std::span<const TypeId> arguments) {
	std::vector<CanonicalTemplateArgument> mixed;
	mixed.reserve(arguments.size());
	for (const TypeId argument : arguments) {
		mixed.push_back(CanonicalTemplateArgument::makeType(argument));
	}
	return aliasTemplateSpecialization(primary, mixed);
}

void CanonicalTypeTable::publishAliasTemplateTarget(TemplateDeclId primary, TypeId target,
	std::span<const CanonicalTemplateArgKind> parameter_kinds) {
	publishAliasTemplateTarget(primary, TemplateDeclId{}, target, parameter_kinds);
}

void CanonicalTypeTable::publishAliasTemplateTarget(TemplateDeclId primary, TemplateDeclId owner,
	TypeId target, std::span<const CanonicalTemplateArgKind> parameter_kinds) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!primary || !target || isInternalLink(nodeUnlocked(target).kind)) {
		throw InternalError("canonical type: invalid alias target publication");
	}
	AliasTemplateTarget published{target, owner, {}};
	published.parameter_kinds.assign(parameter_kinds.begin(), parameter_kinds.end());
	const auto [it, inserted] = alias_template_targets_.emplace(primary.value, std::move(published));
	if (!inserted && (it->second.target != target || it->second.owner != owner ||
		it->second.parameter_kinds.size() != parameter_kinds.size() ||
		!std::equal(it->second.parameter_kinds.begin(), it->second.parameter_kinds.end(),
			parameter_kinds.begin()))) {
		throw InternalError("canonical type: conflicting alias target publication");
	}
}

std::optional<TypeId> CanonicalTypeTable::resolveMemberAliasTarget(TemplateDeclId member,
	TypeId owner_specialization, std::span<const TypeId> alias_args) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return resolveMemberAliasTargetUnlocked(member, owner_specialization, alias_args);
}

std::optional<TypeId> CanonicalTypeTable::resolveMemberAliasUse(TypeId use) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return resolveMemberAliasUseUnlocked(use);
}

std::optional<TypeId> CanonicalTypeTable::aliasTemplateTarget(TemplateDeclId primary) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto it = alias_template_targets_.find(primary.value);
	return it == alias_template_targets_.end() ? std::nullopt : std::optional<TypeId>(it->second.target);
}

TypeId CanonicalTypeTable::templateSpecialization(TemplateDeclId primary, std::span<const TypeId> arguments) {
	std::vector<CanonicalTemplateArgument> mixed;
	mixed.reserve(arguments.size());
	for (const TypeId argument : arguments) {
		mixed.push_back(CanonicalTemplateArgument::makeType(argument));
	}
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!primary) {
		throw InternalError("canonical type: invalid specialization TemplateDeclId");
	}
	return internUnlocked({
		.child = rebuildMixedTemplateArgListUnlocked(mixed),
		.kind = CanonicalTypeKind::TemplateSpecialization,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = primary.value,
	});
}

TypeId CanonicalTypeTable::dependentName(TypeId qualifier, std::string_view identifier) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!isDependentQualifierKind(nodeUnlocked(qualifier).kind)) {
		throw InternalError("canonical type: unsupported dependent-name qualifier");
	}
	return internUnlocked({
		.child = qualifier,
		.kind = CanonicalTypeKind::DependentName,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = packIdentifierBytesUnlocked(identifier).value,
	});
}

TypeId CanonicalTypeTable::dependentTemplateMember(
	TypeId qualifier,
	std::string_view identifier,
	std::span<const TypeId> arguments) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!isDependentQualifierKind(nodeUnlocked(qualifier).kind)) {
		throw InternalError("canonical type: unsupported dependent template-member qualifier");
	}
	TypeId arg_link{};
	for (size_t index = arguments.size(); index-- > 0;) {
		const TypeId argument = arguments[index];
		const CanonicalTypeNode argument_node = nodeUnlocked(argument);
		if (isInternalLink(argument_node.kind) ||
			argument_node.kind == CanonicalTypeKind::Array) {
			throw InternalError("canonical type: invalid dependent template-member argument");
		}
		arg_link = internUnlocked({
			.child = arg_link,
			.kind = CanonicalTypeKind::TemplateArg,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = argument.value,
		});
	}
	return internUnlocked({
		.child = qualifier,
		.kind = CanonicalTypeKind::DependentTemplateMember,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = packDependentTemplateMemberExtent(
			packIdentifierBytesUnlocked(identifier), arg_link),
	});
}

TypeId CanonicalTypeTable::dependentMemberAlias(
	TypeId qualifier,
	TemplateDeclId member,
	std::span<const TypeId> arguments) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!member) {
		throw InternalError("canonical type: invalid dependent member alias declaration");
	}
	if (!isDependentQualifierKind(nodeUnlocked(qualifier).kind)) {
		throw InternalError("canonical type: unsupported dependent member alias qualifier");
	}
	TypeId arg_link{};
	for (size_t index = arguments.size(); index-- > 0;) {
		const TypeId argument = arguments[index];
		const CanonicalTypeNode argument_node = nodeUnlocked(argument);
		if (isInternalLink(argument_node.kind) ||
			argument_node.kind == CanonicalTypeKind::Array) {
			throw InternalError("canonical type: invalid dependent member alias argument");
		}
		arg_link = internUnlocked({
			.child = arg_link,
			.kind = CanonicalTypeKind::TemplateArg,
			.builtin = CanonicalBuiltinKind::Void,
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = argument.value,
		});
	}
	return internUnlocked({
		.child = qualifier,
		.kind = CanonicalTypeKind::DependentMemberAlias,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = packDependentMemberAliasExtent(member, arg_link),
	});
}

TypeId CanonicalTypeTable::dependentNameQualifier(TypeId type) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(type);
	if (input.kind != CanonicalTypeKind::DependentName &&
		input.kind != CanonicalTypeKind::DependentTemplateMember &&
		input.kind != CanonicalTypeKind::DependentMemberAlias) {
		throw InternalError("canonical type: expected dependent name");
	}
	return input.child;
}

std::string CanonicalTypeTable::dependentNameIdentifier(TypeId type) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(type);
	TypeId name_link{};
	if (input.kind == CanonicalTypeKind::DependentName) {
		name_link = TypeId{static_cast<uint32_t>(input.array_extent)};
	} else if (input.kind == CanonicalTypeKind::DependentTemplateMember) {
		name_link = unpackDependentTemplateMemberName(input.array_extent);
	} else {
		throw InternalError("canonical type: expected dependent name");
	}
	std::string identifier;
	TypeId cursor = name_link;
	while (cursor) {
		const auto bytes = nodeUnlocked(cursor);
		for (uint8_t index = 0; index < static_cast<uint8_t>(bytes.builtin); ++index) {
			identifier.push_back(static_cast<char>((bytes.array_extent >> (index * 8)) & 0xff));
		}
		cursor = bytes.child;
	}
	return identifier;
}

TypeId CanonicalTypeTable::dependentTemplateMemberArguments(TypeId type) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(type);
	if (input.kind != CanonicalTypeKind::DependentTemplateMember) {
		throw InternalError("canonical type: expected dependent template member");
	}
	return unpackDependentTemplateMemberArgs(input.array_extent);
}

TemplateDeclId CanonicalTypeTable::dependentMemberAliasDecl(TypeId type) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(type);
	if (input.kind != CanonicalTypeKind::DependentMemberAlias) {
		throw InternalError("canonical type: expected dependent member alias");
	}
	return unpackDependentMemberAliasDecl(input.array_extent);
}

TypeId CanonicalTypeTable::dependentMemberAliasArguments(TypeId type) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(type);
	if (input.kind != CanonicalTypeKind::DependentMemberAlias) {
		throw InternalError("canonical type: expected dependent member alias");
	}
	return unpackDependentMemberAliasArgs(input.array_extent);
}

TypeId CanonicalTypeTable::substitute(TypeId type, TemplateDeclId env, std::span<const TypeId> args) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	TemplateVector<CanonicalTemplateArgument, 8> arguments;
	arguments.reserve(args.size());
	for (const TypeId argument : args) {
		arguments.push_back(CanonicalTemplateArgument::makeType(argument));
	}
	return resolveNestedAliasGraphUnlocked(substituteArgumentsUnlocked(
		type, env,
		std::span<const CanonicalTemplateArgument>(arguments.data(), arguments.size())));
}

TypeId CanonicalTypeTable::memberObjectPointer(TypeId owner, TypeId pointee) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const TypeId record_owner = recordOwnerUnlocked(owner);
	const CanonicalTypeNode pointee_node = nodeUnlocked(pointee);
	if (pointee_node.kind == CanonicalTypeKind::Function ||
		isInternalLink(pointee_node.kind) ||
		(pointee_node.kind == CanonicalTypeKind::Builtin &&
			pointee_node.builtin == CanonicalBuiltinKind::Void)) {
		throw InternalError("canonical type: invalid member object pointee");
	}
	return internUnlocked({
		.child = pointee,
		.kind = CanonicalTypeKind::MemberObjectPointer,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = record_owner.value,
	});
}

TypeId CanonicalTypeTable::memberFunctionPointer(TypeId owner, TypeId function) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const TypeId record_owner = recordOwnerUnlocked(owner);
	if (nodeUnlocked(function).kind != CanonicalTypeKind::Function) {
		throw InternalError("canonical type: member function pointee must be a function type");
	}
	return internUnlocked({
		.child = function,
		.kind = CanonicalTypeKind::MemberFunctionPointer,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = CanonicalTypeNodeFlags::None,
		.array_extent = record_owner.value,
	});
}

TypeId CanonicalTypeTable::withoutTopLevelQualifiers(TypeId id) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(id);
	return input.kind == CanonicalTypeKind::Qualified ? input.child : id;
}

CanonicalTypeNode CanonicalTypeTable::node(TypeId id) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return nodeUnlocked(id);
}

TypeId CanonicalTypeTable::functionParameterType(TypeId param_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(param_link);
	if (input.kind != CanonicalTypeKind::FunctionParam) {
		throw InternalError("canonical type: TypeId is not a function parameter link");
	}
	if (input.array_extent == 0 || input.array_extent > live_count_) {
		throw InternalError("canonical type: function parameter TypeId is outside this table");
	}
	return TypeId{static_cast<uint32_t>(input.array_extent)};
}

TypeId CanonicalTypeTable::functionParameterNext(TypeId param_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(param_link);
	if (input.kind != CanonicalTypeKind::FunctionParam) {
		throw InternalError("canonical type: TypeId is not a function parameter link");
	}
	return input.child;
}

TypeId CanonicalTypeTable::functionParameters(TypeId function) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(function);
	if (input.kind != CanonicalTypeKind::Function) {
		throw InternalError("canonical type: TypeId is not a function type");
	}
	const TypeId param_link = unpackFunctionParamLink(input.array_extent);
	if (!param_link) {
		return {};
	}
	if (param_link.value > live_count_) {
		throw InternalError("canonical type: function parameter list is outside this table");
	}
	return param_link;
}

ExprId CanonicalTypeTable::functionDependentNoexcept(TypeId function) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(function);
	if (input.kind != CanonicalTypeKind::Function) {
		throw InternalError("canonical type: TypeId is not a function type");
	}
	const ExprId dependent = unpackFunctionDependentNoexcept(input.array_extent);
	const bool flagged = hasCanonicalTypeNodeFlag(
		input.flags, CanonicalTypeNodeFlags::DependentNoexceptFunction);
	if (flagged != static_cast<bool>(dependent)) {
		throw InternalError("canonical type: dependent noexcept flag/extent mismatch");
	}
	return dependent;
}

TypeId CanonicalTypeTable::memberPointerOwner(TypeId member_pointer) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(member_pointer);
	if (input.kind != CanonicalTypeKind::MemberObjectPointer &&
		input.kind != CanonicalTypeKind::MemberFunctionPointer) {
		throw InternalError("canonical type: TypeId is not a member pointer");
	}
	if (input.array_extent == 0 || input.array_extent > live_count_) {
		throw InternalError("canonical type: member pointer owner is outside this table");
	}
	return TypeId{static_cast<uint32_t>(input.array_extent)};
}

TypeId CanonicalTypeTable::memberPointerPointee(TypeId member_pointer) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(member_pointer);
	if (input.kind != CanonicalTypeKind::MemberObjectPointer &&
		input.kind != CanonicalTypeKind::MemberFunctionPointer) {
		throw InternalError("canonical type: TypeId is not a member pointer");
	}
	return input.child;
}

EntityId CanonicalTypeTable::recordEntity(TypeId record) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(record);
	if (input.kind != CanonicalTypeKind::Record) {
		throw InternalError("canonical type: TypeId is not a record");
	}
	return EntityId{static_cast<uint32_t>(input.array_extent)};
}

EntityId CanonicalTypeTable::enumEntity(TypeId enumeration) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(enumeration);
	if (input.kind != CanonicalTypeKind::Enum) {
		throw InternalError("canonical type: TypeId is not an enum");
	}
	return EntityId{static_cast<uint32_t>(input.array_extent)};
}

TemplateDeclId CanonicalTypeTable::templateParameterDecl(TypeId parameter) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(parameter);
	if (input.kind != CanonicalTypeKind::TemplateParameter) {
		throw InternalError("canonical type: TypeId is not a template parameter");
	}
	return unpackTemplateParameterDecl(input.array_extent);
}

uint32_t CanonicalTypeTable::templateParameterIndex(TypeId parameter) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(parameter);
	if (input.kind != CanonicalTypeKind::TemplateParameter) {
		throw InternalError("canonical type: TypeId is not a template parameter");
	}
	return unpackTemplateParameterIndex(input.array_extent);
}

bool CanonicalTypeTable::dependsOnlyOnTemplateParameters(TypeId type, TemplateDeclId template_decl) const {
	return dependsOnlyOnTemplateParameters(type, template_decl, TemplateDeclId{});
}

bool CanonicalTypeTable::dependsOnlyOnTemplateParameters(TypeId type, TemplateDeclId template_decl,
	TemplateDeclId owner_decl) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!type || !template_decl) {
		throw InternalError("canonical type: invalid template-parameter dependency query");
	}
	TemplateVector<TypeId, 4> pending;
	pending.push_back(type);
	while (!pending.empty()) {
		const CanonicalTypeNode node = nodeUnlocked(pending.back());
		pending.pop_back();
		if (node.kind == CanonicalTypeKind::TemplateParameter &&
			unpackTemplateParameterDecl(node.array_extent) != template_decl &&
			unpackTemplateParameterDecl(node.array_extent) != owner_decl) {
			return false;
		}
		if (node.kind == CanonicalTypeKind::DependentTemplateTemplateArg &&
			unpackTemplateParameterDecl(node.array_extent) != template_decl &&
			unpackTemplateParameterDecl(node.array_extent) != owner_decl) {
			return false;
		}
		if (node.kind == CanonicalTypeKind::FunctionParam ||
			node.kind == CanonicalTypeKind::TemplateArg) {
			pending.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
		} else if (isMemberPointer(node.kind)) {
			pending.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
		}
		if (node.kind == CanonicalTypeKind::Function) {
			pending.push_back(unpackFunctionParamLink(node.array_extent));
		}
		if (node.child) {
			pending.push_back(node.child);
		}
	}
	return true;
}

TemplateDeclId CanonicalTypeTable::templateSpecializationDecl(TypeId specialization) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(specialization);
	if (input.kind != CanonicalTypeKind::TemplateSpecialization &&
		input.kind != CanonicalTypeKind::AliasTemplateSpecialization) {
		throw InternalError("canonical type: TypeId is not a template specialization");
	}
	return TemplateDeclId{static_cast<uint32_t>(input.array_extent)};
}

TypeId CanonicalTypeTable::templateSpecializationArguments(TypeId specialization) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(specialization);
	if (input.kind != CanonicalTypeKind::TemplateSpecialization &&
		input.kind != CanonicalTypeKind::AliasTemplateSpecialization) {
		throw InternalError("canonical type: TypeId is not a template specialization");
	}
	if (!input.child) {
		return TypeId{};
	}
	if (input.child.value > live_count_) {
		throw InternalError("canonical type: invalid template argument link");
	}
	return input.child;
}

TypeId CanonicalTypeTable::templateArgumentType(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::TemplateArg) {
		throw InternalError("canonical type: TypeId is not a type template argument link");
	}
	return TypeId{static_cast<uint32_t>(input.array_extent)};
}

ExprId CanonicalTypeTable::templateArgumentExpr(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::NonTypeTemplateArg) {
		throw InternalError("canonical type: TypeId is not a non-type template argument link");
	}
	return ExprId{static_cast<uint32_t>(input.array_extent)};
}

TemplateDeclId CanonicalTypeTable::templateArgumentTemplate(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::TemplateTemplateArg) {
		throw InternalError("canonical type: TypeId is not a template-template argument link");
	}
	return TemplateDeclId{static_cast<uint32_t>(input.array_extent)};
}

TemplateDeclId CanonicalTypeTable::templateArgumentDependentTemplateDecl(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::DependentTemplateTemplateArg) {
		throw InternalError("canonical type: TypeId is not a dependent template-template argument link");
	}
	return unpackTemplateParameterDecl(input.array_extent);
}

uint32_t CanonicalTypeTable::templateArgumentDependentTemplateIndex(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::DependentTemplateTemplateArg) {
		throw InternalError("canonical type: TypeId is not a dependent template-template argument link");
	}
	return unpackTemplateParameterIndex(input.array_extent);
}

CanonicalTemplateArgKind CanonicalTypeTable::templateArgumentKind(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind == CanonicalTypeKind::TemplateArg) {
		return CanonicalTemplateArgKind::Type;
	}
	if (input.kind == CanonicalTypeKind::NonTypeTemplateArg) {
		return CanonicalTemplateArgKind::NonType;
	}
	if (input.kind == CanonicalTypeKind::TemplateTemplateArg) {
		return CanonicalTemplateArgKind::Template;
	}
	if (input.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
		return CanonicalTemplateArgKind::DependentTemplate;
	}
	throw InternalError("canonical type: TypeId is not a template argument link");
}

bool CanonicalTypeTable::templateArgumentIsType(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind == CanonicalTypeKind::TemplateArg) {
		return true;
	}
	if (input.kind == CanonicalTypeKind::NonTypeTemplateArg) {
		return false;
	}
	if (input.kind == CanonicalTypeKind::TemplateTemplateArg) {
		return false;
	}
	if (input.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
		return false;
	}
	throw InternalError("canonical type: TypeId is not a template argument link");
}

TypeId CanonicalTypeTable::templateArgumentNext(TypeId arg_link) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto input = nodeUnlocked(arg_link);
	if (input.kind != CanonicalTypeKind::TemplateArg &&
		input.kind != CanonicalTypeKind::NonTypeTemplateArg &&
		input.kind != CanonicalTypeKind::TemplateTemplateArg &&
		input.kind != CanonicalTypeKind::DependentTemplateTemplateArg) {
		throw InternalError("canonical type: TypeId is not a template argument link");
	}
	return input.child;
}

void CanonicalTypeTable::publishRecordLayout(CanonicalRecordLayout layout) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!layout.entity || layout.size_bytes == 0 || layout.alignment == 0 ||
		(layout.alignment & (layout.alignment - 1u)) != 0 ||
		layout.layout_data_size_bytes > layout.size_bytes ||
		layout.non_virtual_size_bytes > layout.size_bytes) {
		throw InternalError("canonical type: invalid complete record layout");
	}
	publishLayoutUnlocked(record_layouts_, live_record_layout_count_, record_layout_ids_, layout,
		"canonical type: conflicting record layout publication");
	noteArenaBytes();
}

void CanonicalTypeTable::publishEnumLayout(CanonicalEnumLayout layout) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!layout.entity || !layout.underlying_type || layout.size_bytes == 0) {
		throw InternalError("canonical type: invalid complete enum layout");
	}
	const CanonicalTypeNode underlying = nodeUnlocked(layout.underlying_type);
	if (underlying.kind != CanonicalTypeKind::Builtin ||
		underlying.builtin == CanonicalBuiltinKind::Void ||
		underlying.builtin == CanonicalBuiltinKind::Float ||
		underlying.builtin == CanonicalBuiltinKind::Double ||
		underlying.builtin == CanonicalBuiltinKind::LongDouble ||
		underlying.builtin == CanonicalBuiltinKind::Nullptr) {
		throw InternalError("canonical type: enum underlying type is not an integer builtin");
	}
	publishLayoutUnlocked(enum_layouts_, live_enum_layout_count_, enum_layout_ids_, layout,
		"canonical type: conflicting enum layout publication");
	noteArenaBytes();
}

bool CanonicalTypeTable::hasRecordLayout(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return entity && record_layout_ids_.contains(entity.value);
}

bool CanonicalTypeTable::hasEnumLayout(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return entity && enum_layout_ids_.contains(entity.value);
}

CanonicalRecordLayout CanonicalTypeTable::recordLayout(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto found = record_layout_ids_.find(entity.value);
	if (!entity || found == record_layout_ids_.end()) {
		throw InternalError("canonical type: record has no complete layout");
	}
	return record_layouts_[found->second];
}

CanonicalEnumLayout CanonicalTypeTable::enumLayout(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const auto found = enum_layout_ids_.find(entity.value);
	if (!entity || found == enum_layout_ids_.end()) {
		throw InternalError("canonical type: enum has no complete layout");
	}
	return enum_layouts_[found->second];
}

void CanonicalTypeTable::publishRecordFieldSchema(EntityId entity,
	std::span<const CanonicalRecordMember> members,
	std::span<const CanonicalRecordBase> bases) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!entity) {
		throw InternalError("canonical type: invalid record field schema entity");
	}
	const auto layout_found = record_layout_ids_.find(entity.value);
	if (layout_found == record_layout_ids_.end()) {
		throw InternalError("canonical type: record field schema requires complete layout");
	}
	const CanonicalRecordLayout layout = record_layouts_[layout_found->second];
	if (members.size() != layout.member_count || bases.size() != layout.direct_base_count) {
		throw InternalError("canonical type: record field schema count mismatch");
	}
	for (const CanonicalRecordMember& member : members) {
		validateRecordMemberUnlocked(member);
	}
	for (const CanonicalRecordBase& base : bases) {
		validateRecordBaseUnlocked(base);
	}
	const auto existing = record_field_schema_ids_.find(entity.value);
	if (existing != record_field_schema_ids_.end()) {
		const CanonicalRecordFieldSchemaHeader header =
			record_field_schema_headers_[existing->second];
		if (header.member_count != members.size() || header.base_count != bases.size()) {
			throw InternalError("canonical type: conflicting record field schema publication");
		}
		for (size_t index = 0; index < members.size(); ++index) {
			if (record_members_[header.member_begin + index] != members[index]) {
				throw InternalError("canonical type: conflicting record field schema publication");
			}
		}
		for (size_t index = 0; index < bases.size(); ++index) {
			if (record_bases_[header.base_begin + index] != bases[index]) {
				throw InternalError("canonical type: conflicting record field schema publication");
			}
		}
		return;
	}
	const uint32_t member_begin = static_cast<uint32_t>(live_record_member_count_);
	const uint32_t base_begin = static_cast<uint32_t>(live_record_base_count_);
	if (static_cast<uint64_t>(member_begin) + members.size() >
			std::numeric_limits<uint32_t>::max() ||
		static_cast<uint64_t>(base_begin) + bases.size() >
			std::numeric_limits<uint32_t>::max()) {
		throw InternalError("canonical type: record field schema arena exhausted");
	}
	for (const CanonicalRecordMember& member : members) {
		appendSchemaEntryUnlocked(record_members_, live_record_member_count_, member);
	}
	for (const CanonicalRecordBase& base : bases) {
		appendSchemaEntryUnlocked(record_bases_, live_record_base_count_, base);
	}
	const CanonicalRecordFieldSchemaHeader header{
		.entity = entity,
		.member_begin = member_begin,
		.base_begin = base_begin,
		.member_count = static_cast<uint16_t>(members.size()),
		.base_count = static_cast<uint16_t>(bases.size()),
	};
	const size_t header_index = live_record_field_schema_count_;
	appendSchemaEntryUnlocked(record_field_schema_headers_, live_record_field_schema_count_,
		header);
	try {
		record_field_schema_ids_.emplace(entity.value, header_index);
	} catch (...) {
		live_record_field_schema_count_ = header_index;
		live_record_member_count_ = member_begin;
		live_record_base_count_ = base_begin;
		noteArenaBytes();
		throw;
	}
	noteArenaBytes();
}

bool CanonicalTypeTable::hasRecordFieldSchema(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return entity && record_field_schema_ids_.contains(entity.value);
}

CanonicalRecordMember CanonicalTypeTable::recordMemberAt(EntityId entity, size_t index) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const CanonicalRecordFieldSchemaHeader header = fieldSchemaHeaderUnlocked(entity);
	if (index >= header.member_count) {
		throw InternalError("canonical type: record member index out of range");
	}
	return record_members_[header.member_begin + index];
}

CanonicalRecordBase CanonicalTypeTable::recordBaseAt(EntityId entity, size_t index) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	const CanonicalRecordFieldSchemaHeader header = fieldSchemaHeaderUnlocked(entity);
	if (index >= header.base_count) {
		throw InternalError("canonical type: record base index out of range");
	}
	return record_bases_[header.base_begin + index];
}

void CanonicalTypeTable::publishRecordNamedTypeMembers(EntityId entity,
	std::span<const CanonicalNamedTypeMemberSpec> members) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (!entity) {
		throw InternalError("canonical type: invalid named type-member schema entity");
	}
	std::vector<CanonicalNamedTypeMember> packed;
	packed.reserve(members.size());
	for (const CanonicalNamedTypeMemberSpec& member : members) {
		if (isInternalLink(nodeUnlocked(member.type).kind)) {
			throw InternalError("canonical type: internal link is not a named type-member type");
		}
		const TypeId name_link = packIdentifierBytesUnlocked(member.name);
		for (const CanonicalNamedTypeMember& prior : packed) {
			if (prior.name == name_link) {
				throw InternalError("canonical type: duplicate named type-member identifier");
			}
		}
		packed.push_back({
			.type = member.type,
			.name = name_link,
		});
	}
	const auto existing = named_type_member_schema_ids_.find(entity.value);
	if (existing != named_type_member_schema_ids_.end()) {
		const CanonicalNamedTypeMemberSchemaHeader header =
			named_type_member_schema_headers_[existing->second];
		if (header.member_count != packed.size()) {
			throw InternalError("canonical type: conflicting named type-member schema");
		}
		for (size_t index = 0; index < packed.size(); ++index) {
			if (named_type_members_[header.member_begin + index] != packed[index]) {
				throw InternalError("canonical type: conflicting named type-member schema");
			}
		}
		return;
	}
	const uint32_t member_begin = static_cast<uint32_t>(live_named_type_member_count_);
	if (static_cast<uint64_t>(member_begin) + packed.size() >
		std::numeric_limits<uint32_t>::max()) {
		throw InternalError("canonical type: named type-member schema arena exhausted");
	}
	for (const CanonicalNamedTypeMember& member : packed) {
		appendSchemaEntryUnlocked(named_type_members_, live_named_type_member_count_, member);
	}
	const CanonicalNamedTypeMemberSchemaHeader header{
		.entity = entity,
		.member_begin = member_begin,
		.member_count = static_cast<uint16_t>(packed.size()),
		.reserved = 0,
		.reserved2 = 0,
	};
	const size_t header_index = live_named_type_member_schema_count_;
	appendSchemaEntryUnlocked(named_type_member_schema_headers_,
		live_named_type_member_schema_count_, header);
	try {
		named_type_member_schema_ids_.emplace(entity.value, header_index);
	} catch (...) {
		live_named_type_member_schema_count_ = header_index;
		live_named_type_member_count_ = member_begin;
		noteArenaBytes();
		throw;
	}
	noteArenaBytes();
}

bool CanonicalTypeTable::hasRecordNamedTypeMembers(EntityId entity) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return entity && named_type_member_schema_ids_.contains(entity.value);
}

std::optional<TypeId> CanonicalTypeTable::tryLookupNamedTypeMember(EntityId entity, std::string_view name) const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return tryLookupNamedTypeMemberUnlocked(entity, name);
}

TypeId CanonicalTypeTable::tryResolveDependentTip(TypeId type) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return tryResolveDependentTipUnlocked(type);
}

size_t CanonicalTypeTable::size() const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return live_count_;
}

CanonicalTypeArenaStats CanonicalTypeTable::arenaStats() const {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	return {usedBytesUnlocked(), reservedBytesUnlocked()};
}

bool CanonicalTypeTable::isReference(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::LValueReference || kind == CanonicalTypeKind::RValueReference;
}

bool CanonicalTypeTable::isInternalLink(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::FunctionParam || kind == CanonicalTypeKind::TemplateArg ||
		kind == CanonicalTypeKind::NonTypeTemplateArg ||
		kind == CanonicalTypeKind::TemplateTemplateArg ||
		kind == CanonicalTypeKind::DependentTemplateTemplateArg ||
		kind == CanonicalTypeKind::NameBytes;
}

bool CanonicalTypeTable::isDependentQualifierKind(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::TemplateParameter ||
		kind == CanonicalTypeKind::TemplateSpecialization ||
		kind == CanonicalTypeKind::AliasTemplateSpecialization ||
		kind == CanonicalTypeKind::DependentName ||
		kind == CanonicalTypeKind::DependentTemplateMember ||
		kind == CanonicalTypeKind::DependentMemberAlias;
}

bool CanonicalTypeTable::isDependentNameFamily(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::DependentName ||
		kind == CanonicalTypeKind::DependentTemplateMember;
}

bool CanonicalTypeTable::isDependentAliasArgumentUnlocked(TypeId type) const {
	TemplateVector<TypeId, 8> pending;
	pending.push_back(type);
	while (!pending.empty()) {
		const CanonicalTypeNode node = nodeUnlocked(pending.back());
		pending.pop_back();
		switch (node.kind) {
		case CanonicalTypeKind::TemplateParameter:
		case CanonicalTypeKind::DependentName:
		case CanonicalTypeKind::DependentTemplateMember:
		case CanonicalTypeKind::DependentMemberAlias:
			return true;
		case CanonicalTypeKind::Qualified:
		case CanonicalTypeKind::Pointer:
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference:
		case CanonicalTypeKind::Array:
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::Function: {
			pending.push_back(node.child);
			for (TypeId link = unpackFunctionParamLink(node.array_extent); link;
				 link = nodeUnlocked(link).child) {
				pending.push_back(
					TypeId{static_cast<uint32_t>(nodeUnlocked(link).array_extent)});
			}
			break;
		}
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer:
			pending.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			for (TypeId link = node.child; link; link = nodeUnlocked(link).child) {
				const CanonicalTypeNode argument = nodeUnlocked(link);
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					pending.push_back(TypeId{static_cast<uint32_t>(argument.array_extent)});
				} else if (argument.kind ==
					CanonicalTypeKind::DependentTemplateTemplateArg) {
					return true;
				}
			}
			break;
		}
		default:
			break;
		}
	}
	return false;
}

bool CanonicalTypeTable::templateParameterReferencesCoveredUnlocked(TypeId type, TemplateDeclId env,
	size_t argument_count) const {
	TemplateVector<TypeId, 8> pending;
	pending.push_back(type);
	while (!pending.empty()) {
		const CanonicalTypeNode node = nodeUnlocked(pending.back());
		pending.pop_back();
		switch (node.kind) {
		case CanonicalTypeKind::TemplateParameter:
		case CanonicalTypeKind::DependentTemplateTemplateArg:
			if (unpackTemplateParameterDecl(node.array_extent) == env &&
				unpackTemplateParameterIndex(node.array_extent) >= argument_count) {
				return false;
			}
			break;
		case CanonicalTypeKind::Qualified:
		case CanonicalTypeKind::Pointer:
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference:
		case CanonicalTypeKind::Array:
		case CanonicalTypeKind::DependentName:
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::Function: {
			pending.push_back(node.child);
			for (TypeId link = unpackFunctionParamLink(node.array_extent); link;
				 link = nodeUnlocked(link).child) {
				pending.push_back(
					TypeId{static_cast<uint32_t>(nodeUnlocked(link).array_extent)});
			}
			break;
		}
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer:
			pending.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			for (TypeId link = node.child; link; link = nodeUnlocked(link).child) {
				const CanonicalTypeNode argument = nodeUnlocked(link);
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					pending.push_back(TypeId{static_cast<uint32_t>(argument.array_extent)});
				} else if (argument.kind ==
					CanonicalTypeKind::DependentTemplateTemplateArg) {
					if (unpackTemplateParameterDecl(argument.array_extent) == env &&
						unpackTemplateParameterIndex(argument.array_extent) >= argument_count) {
						return false;
					}
				}
			}
			break;
		}
		case CanonicalTypeKind::DependentTemplateMember:
		case CanonicalTypeKind::DependentMemberAlias: {
			const TypeId arg_head = node.kind == CanonicalTypeKind::DependentTemplateMember
				? unpackDependentTemplateMemberArgs(node.array_extent)
				: unpackDependentMemberAliasArgs(node.array_extent);
			for (TypeId link = arg_head; link;
				 link = nodeUnlocked(link).child) {
				const CanonicalTypeNode argument = nodeUnlocked(link);
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					pending.push_back(TypeId{static_cast<uint32_t>(argument.array_extent)});
				}
			}
			pending.push_back(node.child);
			break;
		}
		default:
			break;
		}
	}
	return true;
}

bool CanonicalTypeTable::containsNonTypeTemplateArgUnlocked(TypeId type) const {
	TemplateVector<TypeId, 8> pending;
	pending.push_back(type);
	while (!pending.empty()) {
		const CanonicalTypeNode node = nodeUnlocked(pending.back());
		pending.pop_back();
		switch (node.kind) {
		case CanonicalTypeKind::NonTypeTemplateArg:
			return true;
		case CanonicalTypeKind::DependentMemberAlias:
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::Qualified:
		case CanonicalTypeKind::Pointer:
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference:
		case CanonicalTypeKind::Array:
		case CanonicalTypeKind::DependentName:
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::Function: {
			pending.push_back(node.child);
			for (TypeId link = unpackFunctionParamLink(node.array_extent); link;
				 link = nodeUnlocked(link).child) {
				pending.push_back(
					TypeId{static_cast<uint32_t>(nodeUnlocked(link).array_extent)});
			}
			break;
		}
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer:
			pending.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
			pending.push_back(node.child);
			break;
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			for (TypeId link = node.child; link; link = nodeUnlocked(link).child) {
				const CanonicalTypeNode argument = nodeUnlocked(link);
				if (argument.kind == CanonicalTypeKind::NonTypeTemplateArg) {
					return true;
				}
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					pending.push_back(TypeId{static_cast<uint32_t>(argument.array_extent)});
				}
			}
			break;
		}
		default:
			break;
		}
	}
	return false;
}

TypeId CanonicalTypeTable::resolveNestedAliasGraphUnlocked(TypeId type) {
	if (!type) {
		throw InternalError("canonical type: invalid alias graph TypeId");
	}
	if (alias_template_targets_.empty()) {
		return type;
	}

	enum class Phase : uint8_t { Discover, Expand, Build };
	struct Frame {
		TypeId id;
		Phase phase;
		uint32_t scope;
		TypeId expansion_root;
		uint32_t expansion_scope;
	};
	struct Scope {
		uint32_t parent;
		uint32_t decl;
	};

	std::vector<Scope> scopes;
	scopes.push_back({0, 0});
	auto scopeContains = [&scopes](uint32_t scope, uint32_t decl) {
		while (scope != 0) {
			const Scope& current = scopes[scope];
			if (current.decl == decl) {
				return true;
			}
			scope = current.parent;
		}
		return false;
	};
	auto pushScope = [&scopes](uint32_t parent, uint32_t decl) {
		scopes.push_back({parent, decl});
		return static_cast<uint32_t>(scopes.size() - 1);
	};
	auto memoKey = [](uint32_t scope, TypeId id) {
		return (static_cast<uint64_t>(scope) << 32) | id.value;
	};

	std::vector<Frame> stack;
	std::unordered_map<uint64_t, TypeId> memo;
	stack.push_back({type, Phase::Discover, 0, TypeId{}, 0});
	std::vector<CanonicalTemplateArgument> rebuilt_mixed;
	std::vector<TypeId> rebuilt_args;

	while (!stack.empty()) {
		const Frame frame = stack.back();
		if (memo.find(memoKey(frame.scope, frame.id)) != memo.end()) {
			stack.pop_back();
			continue;
		}
		const CanonicalTypeNode node = nodeUnlocked(frame.id);

		if (frame.phase == Phase::Discover) {
			switch (node.kind) {
			case CanonicalTypeKind::Builtin:
			case CanonicalTypeKind::Record:
			case CanonicalTypeKind::Enum:
			case CanonicalTypeKind::TemplateParameter:
				memo.emplace(memoKey(frame.scope, frame.id), frame.id);
				stack.pop_back();
				continue;
			case CanonicalTypeKind::Qualified:
			case CanonicalTypeKind::Pointer:
			case CanonicalTypeKind::LValueReference:
			case CanonicalTypeKind::RValueReference:
			case CanonicalTypeKind::Array:
			case CanonicalTypeKind::DependentName: {
				stack.pop_back();
				Frame parent = frame;
				parent.phase = Phase::Build;
				stack.push_back(parent);
				stack.push_back({node.child, Phase::Discover, frame.scope, TypeId{}, 0});
				continue;
			}
			case CanonicalTypeKind::Function: {
				stack.pop_back();
				Frame parent = frame;
				parent.phase = Phase::Build;
				stack.push_back(parent);
				for (TypeId link = unpackFunctionParamLink(node.array_extent); link;
					 link = nodeUnlocked(link).child) {
					stack.push_back({TypeId{static_cast<uint32_t>(
						nodeUnlocked(link).array_extent)}, Phase::Discover, frame.scope, TypeId{}, 0});
				}
				stack.push_back({node.child, Phase::Discover, frame.scope, TypeId{}, 0});
				continue;
			}
			case CanonicalTypeKind::MemberObjectPointer:
			case CanonicalTypeKind::MemberFunctionPointer: {
				stack.pop_back();
				Frame parent = frame;
				parent.phase = Phase::Build;
				stack.push_back(parent);
				stack.push_back({node.child, Phase::Discover, frame.scope, TypeId{}, 0});
				stack.push_back({TypeId{static_cast<uint32_t>(node.array_extent)},
					Phase::Discover, frame.scope, TypeId{}, 0});
				continue;
			}
			case CanonicalTypeKind::TemplateSpecialization:
			case CanonicalTypeKind::AliasTemplateSpecialization: {
				stack.pop_back();
				Frame parent = frame;
				parent.phase = node.kind == CanonicalTypeKind::AliasTemplateSpecialization
					? Phase::Expand : Phase::Build;
				stack.push_back(parent);
				for (TypeId link = node.child; link; link = nodeUnlocked(link).child) {
					const CanonicalTypeNode argument = nodeUnlocked(link);
					if (argument.kind == CanonicalTypeKind::TemplateArg) {
						stack.push_back({TypeId{static_cast<uint32_t>(argument.array_extent)},
							Phase::Discover, frame.scope, TypeId{}, 0});
					}
				}
				continue;
			}
			case CanonicalTypeKind::DependentTemplateMember:
			case CanonicalTypeKind::DependentMemberAlias: {
				stack.pop_back();
				Frame parent = frame;
				parent.phase = Phase::Build;
				stack.push_back(parent);
				const TypeId arg_head = node.kind == CanonicalTypeKind::DependentTemplateMember
					? unpackDependentTemplateMemberArgs(node.array_extent)
					: unpackDependentMemberAliasArgs(node.array_extent);
				for (TypeId link = arg_head; link; link = nodeUnlocked(link).child) {
					const CanonicalTypeNode argument = nodeUnlocked(link);
					if (argument.kind == CanonicalTypeKind::TemplateArg) {
						stack.push_back({TypeId{static_cast<uint32_t>(argument.array_extent)},
							Phase::Discover, frame.scope, TypeId{}, 0});
					}
				}
				stack.push_back({node.child, Phase::Discover, frame.scope, TypeId{}, 0});
				continue;
			}
			default:
				throw InternalError("canonical type: unsupported alias graph node kind");
			}
		}

		if (frame.phase == Phase::Expand) {
			stack.pop_back();
			TemplateVector<CanonicalTemplateArgument, 4> arguments;
			bool concrete = true;
			for (TypeId link = node.child; link; link = nodeUnlocked(link).child) {
				const CanonicalTypeNode argument = nodeUnlocked(link);
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					const TypeId original{static_cast<uint32_t>(argument.array_extent)};
					const TypeId value = memo.at(memoKey(frame.scope, original));
					if (isDependentAliasArgumentUnlocked(value)) {
						concrete = false;
						break;
					}
					arguments.push_back(CanonicalTemplateArgument::makeType(value));
				} else if (argument.kind == CanonicalTypeKind::NonTypeTemplateArg) {
					arguments.push_back(CanonicalTemplateArgument::makeNonType(
						ExprId{static_cast<uint32_t>(argument.array_extent)}));
				} else if (argument.kind == CanonicalTypeKind::TemplateTemplateArg) {
					arguments.push_back(CanonicalTemplateArgument::makeTemplate(
						TemplateDeclId{static_cast<uint32_t>(argument.array_extent)}));
				} else {
					concrete = false;
					break;
				}
			}
			const uint32_t primary = static_cast<uint32_t>(node.array_extent);
			const auto target = alias_template_targets_.find(primary);
			bool expand = concrete && !scopeContains(frame.scope, primary) &&
				target != alias_template_targets_.end() &&
				target->second.parameter_kinds.size() == arguments.size();
			bool has_non_type_parameter = false;
			if (expand) {
				for (size_t index = 0; index < arguments.size(); ++index) {
					if (arguments[index].kind != target->second.parameter_kinds[index]) {
						expand = false;
						break;
					}
					has_non_type_parameter = has_non_type_parameter ||
						target->second.parameter_kinds[index] ==
							CanonicalTemplateArgKind::NonType;
				}
			}
			if (expand && has_non_type_parameter &&
				containsNonTypeTemplateArgUnlocked(target->second.target)) {
				expand = false;
			}
			if (!expand) {
				Frame parent = frame;
				parent.phase = Phase::Build;
				stack.push_back(parent);
				continue;
			}
			const TypeId expanded = substituteArgumentsUnlocked(
				target->second.target,
				TemplateDeclId{primary},
				std::span<const CanonicalTemplateArgument>(
					arguments.data(), arguments.size()));
			Frame parent = frame;
			parent.phase = Phase::Build;
			parent.expansion_root = expanded;
			parent.expansion_scope = pushScope(frame.scope, primary);
			stack.push_back(parent);
			stack.push_back({expanded, Phase::Discover, parent.expansion_scope, TypeId{}, 0});
			continue;
		}

		stack.pop_back();
		if (frame.expansion_root) {
			const TypeId expanded =
				memo.at(memoKey(frame.expansion_scope, frame.expansion_root));
			memo.emplace(memoKey(frame.scope, frame.id), expanded);
			continue;
		}
		TypeId rebuilt = frame.id;
		switch (node.kind) {
		case CanonicalTypeKind::Qualified: {
			const TypeId child = memo.at(memoKey(frame.scope, node.child));
			if (child != node.child) {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Qualified,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = node.qualifiers,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::Pointer: {
			const TypeId child = memo.at(memoKey(frame.scope, node.child));
			if (child != node.child) {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Pointer,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference: {
			TypeId referent = memo.at(memoKey(frame.scope, node.child));
			CanonicalTypeKind kind = node.kind;
			if (isReference(nodeUnlocked(referent).kind)) {
				const CanonicalTypeNode referent_node = nodeUnlocked(referent);
				if (referent_node.kind == CanonicalTypeKind::LValueReference) {
					kind = CanonicalTypeKind::LValueReference;
				}
				referent = referent_node.child;
			}
			if (referent != node.child || kind != node.kind) {
				rebuilt = internUnlocked({
					.child = referent,
					.kind = kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::Array: {
			const TypeId child = memo.at(memoKey(frame.scope, node.child));
			if (child != node.child) {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Array,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = node.flags,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::DependentName: {
			const TypeId qualifier = memo.at(memoKey(frame.scope, node.child));
			if (qualifier != node.child) {
				if (isInternalLink(nodeUnlocked(qualifier).kind)) {
					throw InternalError(
						"canonical type: normalized dependent-name qualifier is an internal link");
				}
				rebuilt = internUnlocked({
					.child = qualifier,
					.kind = CanonicalTypeKind::DependentName,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			rebuilt_mixed.clear();
			TypeId arg_link = node.child;
			bool unchanged = true;
			while (arg_link) {
				const CanonicalTypeNode argument = nodeUnlocked(arg_link);
				if (argument.kind == CanonicalTypeKind::TemplateArg) {
					const TypeId original{static_cast<uint32_t>(argument.array_extent)};
					const TypeId value = memo.at(memoKey(frame.scope, original));
					unchanged = unchanged && value == original;
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeType(value));
				} else if (argument.kind == CanonicalTypeKind::NonTypeTemplateArg) {
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeNonType(
						ExprId{static_cast<uint32_t>(argument.array_extent)}));
				} else if (argument.kind == CanonicalTypeKind::TemplateTemplateArg) {
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeTemplate(
						TemplateDeclId{static_cast<uint32_t>(argument.array_extent)}));
				} else if (argument.kind ==
					CanonicalTypeKind::DependentTemplateTemplateArg) {
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeDependentTemplate(
						unpackTemplateParameterDecl(argument.array_extent),
						unpackTemplateParameterIndex(argument.array_extent)));
				} else {
					throw InternalError("canonical type: corrupt template argument link");
				}
				arg_link = argument.child;
			}
			if (!unchanged) {
				rebuilt = internUnlocked({
					.child = rebuildMixedTemplateArgListUnlocked(rebuilt_mixed),
					.kind = node.kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::Function: {
			const TypeId substituted_return = memo.at(memoKey(frame.scope, node.child));
			rebuilt_args.clear();
			bool unchanged = substituted_return == node.child;
			TypeId param_link = unpackFunctionParamLink(node.array_extent);
			while (param_link) {
				const CanonicalTypeNode parameter_link_node = nodeUnlocked(param_link);
				const TypeId original =
					TypeId{static_cast<uint32_t>(parameter_link_node.array_extent)};
				const TypeId value = memo.at(memoKey(frame.scope, original));
				unchanged = unchanged && value == original;
				rebuilt_args.push_back(value);
				param_link = parameter_link_node.child;
			}
			if (unchanged) {
				break;
			}
			const CanonicalTypeNode return_node = nodeUnlocked(substituted_return);
			if (return_node.kind == CanonicalTypeKind::Function ||
				isInternalLink(return_node.kind) ||
				return_node.kind == CanonicalTypeKind::Array) {
				throw InternalError(
					"canonical type: normalized invalid function return type");
			}
			TypeId normalized_param_link{};
			for (size_t index = rebuilt_args.size(); index-- > 0;) {
				const TypeId parameter = rebuilt_args[index];
				const CanonicalTypeNode parameter_node = nodeUnlocked(parameter);
				if (parameter_node.kind == CanonicalTypeKind::Function ||
					isInternalLink(parameter_node.kind) ||
					parameter_node.kind == CanonicalTypeKind::Array) {
					throw InternalError(
						"canonical type: normalized undecayed function parameter type");
				}
				normalized_param_link = internUnlocked({
					.child = normalized_param_link,
					.kind = CanonicalTypeKind::FunctionParam,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = parameter.value,
				});
			}
			const CanonicalBuiltinKind normalized_calling_convention = node.builtin;
			rebuilt = internUnlocked({
				.child = substituted_return,
				.kind = CanonicalTypeKind::Function,
				.builtin = normalized_calling_convention,
				.qualifiers = node.qualifiers,
				.flags = node.flags,
				.array_extent = packFunctionArrayExtent(
					normalized_param_link, unpackFunctionDependentNoexcept(node.array_extent)),
			});
			break;
		}
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer: {
			const TypeId original_owner = TypeId{static_cast<uint32_t>(node.array_extent)};
			const TypeId substituted_owner = memo.at(memoKey(frame.scope, original_owner));
			const TypeId substituted_pointee = memo.at(memoKey(frame.scope, node.child));
			if (substituted_owner == original_owner && substituted_pointee == node.child) {
				break;
			}
			if (nodeUnlocked(substituted_owner).kind != CanonicalTypeKind::Record) {
				throw InternalError(
					"canonical type: normalized member pointer owner must be a record");
			}
			if (node.kind == CanonicalTypeKind::MemberFunctionPointer) {
				if (nodeUnlocked(substituted_pointee).kind != CanonicalTypeKind::Function) {
					throw InternalError(
						"canonical type: normalized member function pointee must be a function");
				}
			} else {
				const CanonicalTypeNode pointee_node = nodeUnlocked(substituted_pointee);
				if (pointee_node.kind == CanonicalTypeKind::Function ||
					isInternalLink(pointee_node.kind) ||
					(pointee_node.kind == CanonicalTypeKind::Builtin &&
						pointee_node.builtin == CanonicalBuiltinKind::Void)) {
					throw InternalError(
						"canonical type: normalized invalid member object pointee");
				}
			}
			rebuilt = internUnlocked({
				.child = substituted_pointee,
				.kind = node.kind,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = substituted_owner.value,
			});
			break;
		}
		case CanonicalTypeKind::DependentTemplateMember:
		case CanonicalTypeKind::DependentMemberAlias: {
			const TypeId qualifier = memo.at(memoKey(frame.scope, node.child));
			rebuilt_args.clear();
			TypeId arg_link = node.kind == CanonicalTypeKind::DependentTemplateMember
				? unpackDependentTemplateMemberArgs(node.array_extent)
				: unpackDependentMemberAliasArgs(node.array_extent);
			bool unchanged = qualifier == node.child;
			while (arg_link) {
				const CanonicalTypeNode argument = nodeUnlocked(arg_link);
				if (argument.kind != CanonicalTypeKind::TemplateArg) {
					throw InternalError(
						"canonical type: corrupt dependent member argument link");
				}
				const TypeId original{static_cast<uint32_t>(argument.array_extent)};
				const TypeId value = memo.at(memoKey(frame.scope, original));
				unchanged = unchanged && value == original;
				rebuilt_args.push_back(value);
				arg_link = argument.child;
			}
			if (!unchanged) {
				if (isInternalLink(nodeUnlocked(qualifier).kind)) {
					throw InternalError(
						"canonical type: normalized dependent member qualifier is an internal link");
				}
				const TypeId rebuilt_arg_link = rebuildTemplateArgListUnlocked(rebuilt_args);
				rebuilt = internUnlocked({
					.child = qualifier,
					.kind = node.kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.kind == CanonicalTypeKind::DependentTemplateMember
						? packDependentTemplateMemberExtent(
							unpackDependentTemplateMemberName(node.array_extent), rebuilt_arg_link)
						: packDependentMemberAliasExtent(
							unpackDependentMemberAliasDecl(node.array_extent), rebuilt_arg_link),
				});
			}
			break;
		}
		default:
			throw InternalError("canonical type: unexpected alias graph build kind");
		}
		memo.emplace(memoKey(frame.scope, frame.id), rebuilt);
	}
	return memo.at(memoKey(0, type));
}

TypeId CanonicalTypeTable::packIdentifierBytesUnlocked(std::string_view identifier) {
	if (identifier.empty() || identifier.find('\0') != std::string_view::npos) {
		throw InternalError("canonical type: invalid dependent identifier");
	}
	TypeId name_link{};
	for (size_t end = identifier.size(); end != 0;) {
		const size_t begin = ((end - 1) / 8) * 8;
		uint64_t bytes = 0;
		for (size_t index = begin; index < end; ++index) {
			bytes |= static_cast<uint64_t>(static_cast<unsigned char>(identifier[index])) << ((index - begin) * 8);
		}
		name_link = internUnlocked({
			.child = name_link,
			.kind = CanonicalTypeKind::NameBytes,
			.builtin = static_cast<CanonicalBuiltinKind>(end - begin),
			.qualifiers = CVQualifier::None,
			.flags = CanonicalTypeNodeFlags::None,
			.array_extent = bytes,
		});
		end = begin;
	}
	return name_link;
}

TypeId CanonicalTypeTable::rebuildTemplateArgListUnlocked(std::span<const TypeId> arguments) {
	std::vector<CanonicalTemplateArgument> mixed;
	mixed.reserve(arguments.size());
	for (const TypeId argument : arguments) {
		mixed.push_back(CanonicalTemplateArgument::makeType(argument));
	}
	return rebuildMixedTemplateArgListUnlocked(mixed);
}

TypeId CanonicalTypeTable::rebuildMixedTemplateArgListUnlocked(std::span<const CanonicalTemplateArgument> arguments) {
	TypeId arg_link{};
	for (size_t index = arguments.size(); index-- > 0;) {
		const CanonicalTemplateArgument& argument = arguments[index];
		if (argument.kind == CanonicalTemplateArgKind::Type) {
			if (!argument.type) {
				throw InternalError("canonical type: invalid template specialization type argument");
			}
			const CanonicalTypeNode argument_node = nodeUnlocked(argument.type);
			if (isInternalLink(argument_node.kind) ||
				argument_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: invalid template specialization argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::TemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.type.value,
			});
			continue;
		}
		if (argument.kind == CanonicalTemplateArgKind::NonType) {
			if (!argument.expr) {
				throw InternalError("canonical type: invalid template specialization NTTP ExprId");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::NonTypeTemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.expr.value,
			});
			continue;
		}
		if (argument.kind == CanonicalTemplateArgKind::Template) {
			if (!argument.template_decl) {
				throw InternalError("canonical type: invalid template specialization template argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::TemplateTemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = argument.template_decl.value,
			});
			continue;
		}
		if (argument.kind == CanonicalTemplateArgKind::DependentTemplate) {
			if (!argument.template_decl) {
				throw InternalError("canonical type: invalid dependent template-template specialization argument");
			}
			arg_link = internUnlocked({
				.child = arg_link,
				.kind = CanonicalTypeKind::DependentTemplateTemplateArg,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = packTemplateParameterExtent(
					argument.template_decl,
					argument.template_parameter_index),
			});
			continue;
		}
		throw InternalError("canonical type: invalid template argument kind");
	}
	return arg_link;
}

TypeId CanonicalTypeTable::substituteArgumentsUnlocked(TypeId type, TemplateDeclId env,
	std::span<const CanonicalTemplateArgument> args) {
	if (!type) {
		throw InternalError("canonical type: invalid substitute TypeId");
	}
	if (!env) {
		throw InternalError("canonical type: invalid substitute TemplateDeclId");
	}
	for (const CanonicalTemplateArgument& argument : args) {
		if (argument.kind == CanonicalTemplateArgKind::Type) {
			if (!argument.type) {
				throw InternalError("canonical type: invalid substitute argument TypeId");
			}
			const CanonicalTypeKind argument_kind = nodeUnlocked(argument.type).kind;
			if (isInternalLink(argument_kind)) {
				throw InternalError("canonical type: substitute argument is an internal link");
			}
		} else if (argument.kind == CanonicalTemplateArgKind::NonType) {
			if (!argument.expr) {
				throw InternalError("canonical type: invalid substitute NTTP ExprId");
			}
		} else if (!argument.template_decl) {
			throw InternalError("canonical type: invalid substitute template argument");
		}
	}

	struct Frame {
		TypeId id;
		bool building;
	};
	std::vector<Frame> stack;
	std::unordered_map<uint32_t, TypeId> memo;
	stack.push_back({type, false});
	std::vector<TypeId> rebuilt_args;
	std::vector<CanonicalTemplateArgument> rebuilt_mixed;

	while (!stack.empty()) {
		Frame frame = stack.back();
		if (const auto existing = memo.find(frame.id.value); existing != memo.end()) {
			stack.pop_back();
			continue;
		}
		const CanonicalTypeNode node = nodeUnlocked(frame.id);
		if (!frame.building) {
			switch (node.kind) {
			case CanonicalTypeKind::Builtin:
			case CanonicalTypeKind::Record:
			case CanonicalTypeKind::Enum:
				memo.emplace(frame.id.value, frame.id);
				stack.pop_back();
				continue;
			case CanonicalTypeKind::TemplateParameter: {
				const TemplateDeclId decl = unpackTemplateParameterDecl(node.array_extent);
				if (decl != env) {
					memo.emplace(frame.id.value, frame.id);
					stack.pop_back();
					continue;
				}
				const uint32_t index = unpackTemplateParameterIndex(node.array_extent);
				if (index >= args.size()) {
					throw InternalError("canonical type: substitute argument index out of range");
				}
				if (args[index].kind != CanonicalTemplateArgKind::Type) {
					throw InternalError(
						"canonical type: type parameter mapped to non-type argument");
				}
				memo.emplace(frame.id.value, args[index].type);
				stack.pop_back();
				continue;
			}
			case CanonicalTypeKind::Function: {
				// Return type plus every parameter element. The FunctionParam
				// links themselves are rebuilt during the build pass.
				stack.back().building = true;
				TypeId param_link = unpackFunctionParamLink(node.array_extent);
				while (param_link) {
					const CanonicalTypeNode parameter_node = nodeUnlocked(param_link);
					if (parameter_node.kind != CanonicalTypeKind::FunctionParam) {
						throw InternalError("canonical type: corrupt function parameter link");
					}
					stack.push_back(
						{TypeId{static_cast<uint32_t>(parameter_node.array_extent)}, false});
					param_link = parameter_node.child;
				}
				stack.push_back({node.child, false});
				continue;
			}
			case CanonicalTypeKind::MemberObjectPointer:
			case CanonicalTypeKind::MemberFunctionPointer: {
				stack.back().building = true;
				stack.push_back({node.child, false});
				stack.push_back({TypeId{static_cast<uint32_t>(node.array_extent)}, false});
				continue;
			}
			case CanonicalTypeKind::FunctionParam:
			case CanonicalTypeKind::TemplateArg:
			case CanonicalTypeKind::NonTypeTemplateArg:
			case CanonicalTypeKind::TemplateTemplateArg:
			case CanonicalTypeKind::DependentTemplateTemplateArg:
			case CanonicalTypeKind::NameBytes:
				throw InternalError("canonical type: substitute root cannot be an internal link");
			case CanonicalTypeKind::Qualified:
			case CanonicalTypeKind::Pointer:
			case CanonicalTypeKind::LValueReference:
			case CanonicalTypeKind::RValueReference:
			case CanonicalTypeKind::Array:
			case CanonicalTypeKind::DependentName:
				stack.back().building = true;
				stack.push_back({node.child, false});
				continue;
			case CanonicalTypeKind::TemplateSpecialization:
			case CanonicalTypeKind::AliasTemplateSpecialization:
			case CanonicalTypeKind::DependentTemplateMember:
			case CanonicalTypeKind::DependentMemberAlias: {
				stack.back().building = true;
				TypeId arg_link{};
				if (node.kind == CanonicalTypeKind::TemplateSpecialization ||
					node.kind == CanonicalTypeKind::AliasTemplateSpecialization) {
					arg_link = node.child;
				} else if (node.kind == CanonicalTypeKind::DependentTemplateMember) {
					arg_link = unpackDependentTemplateMemberArgs(node.array_extent);
				} else {
					arg_link = unpackDependentMemberAliasArgs(node.array_extent);
				}
				while (arg_link) {
					const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
					if (arg_node.kind == CanonicalTypeKind::TemplateArg) {
						stack.push_back({TypeId{static_cast<uint32_t>(arg_node.array_extent)}, false});
					} else if (arg_node.kind == CanonicalTypeKind::NonTypeTemplateArg ||
						arg_node.kind == CanonicalTypeKind::TemplateTemplateArg ||
						arg_node.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
						if (node.kind == CanonicalTypeKind::DependentTemplateMember) {
							throw InternalError("canonical type: dependent template-member non-type/template args are deferred");
						}
						if (node.kind == CanonicalTypeKind::DependentMemberAlias) {
							throw InternalError("canonical type: dependent member-alias non-type/template args are unsupported");
						}
						// Opaque Spec non-type/template arguments do not enter the substitute graph.
					} else {
						throw InternalError("canonical type: corrupt template argument link");
					}
					arg_link = arg_node.child;
				}
				if (node.kind == CanonicalTypeKind::DependentTemplateMember ||
					node.kind == CanonicalTypeKind::DependentMemberAlias) {
					stack.push_back({node.child, false});
				}
				continue;
			}
			}
			throw InternalError("canonical type: unsupported substitute node kind");
		}

		stack.pop_back();
		TypeId rebuilt = frame.id;
		switch (node.kind) {
		case CanonicalTypeKind::Qualified: {
			const TypeId child = memo.at(node.child.value);
			if (child == node.child) {
				rebuilt = frame.id;
			} else {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Qualified,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = node.qualifiers,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::Pointer: {
			const TypeId child = memo.at(node.child.value);
			if (child == node.child) {
				rebuilt = frame.id;
			} else {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Pointer,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::LValueReference:
		case CanonicalTypeKind::RValueReference: {
			TypeId referent = memo.at(node.child.value);
			CanonicalTypeKind kind = node.kind;
			CanonicalTypeNode referent_node = nodeUnlocked(referent);
			if (isReference(referent_node.kind)) {
				if (referent_node.kind == CanonicalTypeKind::LValueReference) {
					kind = CanonicalTypeKind::LValueReference;
				}
				referent = referent_node.child;
				referent_node = nodeUnlocked(referent);
			}
			if (referent == node.child && kind == node.kind) {
				rebuilt = frame.id;
			} else {
				rebuilt = internUnlocked({
					.child = referent,
					.kind = kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = 0,
				});
			}
			break;
		}
		case CanonicalTypeKind::Array: {
			const TypeId child = memo.at(node.child.value);
			if (child == node.child) {
				rebuilt = frame.id;
			} else {
				rebuilt = internUnlocked({
					.child = child,
					.kind = CanonicalTypeKind::Array,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = node.flags,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::DependentName: {
			const TypeId qualifier = memo.at(node.child.value);
			if (qualifier == node.child) {
				rebuilt = frame.id;
			} else if (isInternalLink(nodeUnlocked(qualifier).kind)) {
				throw InternalError("canonical type: substituted dependent-name qualifier is an internal link");
			} else {
				// Concrete qualifiers remain DependentName tips until lookup.
				rebuilt = internUnlocked({
					.child = qualifier,
					.kind = CanonicalTypeKind::DependentName,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::TemplateSpecialization:
		case CanonicalTypeKind::AliasTemplateSpecialization: {
			rebuilt_mixed.clear();
			rebuilt_args.clear();
			TypeId arg_link = node.child;
			bool unchanged = true;
			while (arg_link) {
				const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
				if (arg_node.kind == CanonicalTypeKind::TemplateArg) {
					const TypeId original = TypeId{static_cast<uint32_t>(arg_node.array_extent)};
					const TypeId substituted = memo.at(original.value);
					unchanged = unchanged && substituted == original;
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeType(substituted));
					rebuilt_args.push_back(substituted);
				} else if (arg_node.kind == CanonicalTypeKind::NonTypeTemplateArg) {
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeNonType(
						ExprId{static_cast<uint32_t>(arg_node.array_extent)}));
				} else if (arg_node.kind == CanonicalTypeKind::TemplateTemplateArg) {
					rebuilt_mixed.push_back(CanonicalTemplateArgument::makeTemplate(
						TemplateDeclId{static_cast<uint32_t>(arg_node.array_extent)}));
				} else if (arg_node.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
					const TemplateDeclId placeholder_decl =
						unpackTemplateParameterDecl(arg_node.array_extent);
					const uint32_t placeholder_index =
						unpackTemplateParameterIndex(arg_node.array_extent);
					if (placeholder_decl == env && placeholder_index < args.size() &&
						args[placeholder_index].kind == CanonicalTemplateArgKind::Template) {
						rebuilt_mixed.push_back(CanonicalTemplateArgument::makeTemplate(
							args[placeholder_index].template_decl));
						unchanged = false;
					} else {
						rebuilt_mixed.push_back(CanonicalTemplateArgument::makeDependentTemplate(
							placeholder_decl, placeholder_index));
					}
				} else {
					throw InternalError("canonical type: corrupt template argument link");
				}
				arg_link = arg_node.child;
			}
			if (unchanged) {
				rebuilt = frame.id;
			} else {
				rebuilt = internUnlocked({
					.child = rebuildMixedTemplateArgListUnlocked(rebuilt_mixed),
					.kind = node.kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.array_extent,
				});
			}
			break;
		}
		case CanonicalTypeKind::DependentTemplateMember:
		case CanonicalTypeKind::DependentMemberAlias: {
			const TypeId qualifier = memo.at(node.child.value);
			rebuilt_args.clear();
			TypeId arg_link = node.kind == CanonicalTypeKind::DependentTemplateMember
				? unpackDependentTemplateMemberArgs(node.array_extent)
				: unpackDependentMemberAliasArgs(node.array_extent);
			bool unchanged = qualifier == node.child;
			while (arg_link) {
				const CanonicalTypeNode arg_node = nodeUnlocked(arg_link);
				const TypeId original = TypeId{static_cast<uint32_t>(arg_node.array_extent)};
				const TypeId substituted = memo.at(original.value);
				unchanged = unchanged && substituted == original;
				rebuilt_args.push_back(substituted);
				arg_link = arg_node.child;
			}
			if (unchanged) {
				rebuilt = frame.id;
			} else if (isInternalLink(nodeUnlocked(qualifier).kind)) {
				throw InternalError("canonical type: substituted dependent member qualifier is an internal link");
			} else {
				const TypeId rebuilt_arg_link = rebuildTemplateArgListUnlocked(rebuilt_args);
				rebuilt = internUnlocked({
					.child = qualifier,
					.kind = node.kind,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = node.kind == CanonicalTypeKind::DependentTemplateMember
						? packDependentTemplateMemberExtent(
							unpackDependentTemplateMemberName(node.array_extent), rebuilt_arg_link)
						: packDependentMemberAliasExtent(
							unpackDependentMemberAliasDecl(node.array_extent), rebuilt_arg_link),
				});
			}
			break;
		}
		case CanonicalTypeKind::Function: {
			const TypeId substituted_return = memo.at(node.child.value);
			rebuilt_args.clear();
			bool unchanged = substituted_return == node.child;
			TypeId param_link = unpackFunctionParamLink(node.array_extent);
			while (param_link) {
				const CanonicalTypeNode parameter_link_node = nodeUnlocked(param_link);
				const TypeId original =
					TypeId{static_cast<uint32_t>(parameter_link_node.array_extent)};
				const TypeId substituted = memo.at(original.value);
				unchanged = unchanged && substituted == original;
				rebuilt_args.push_back(substituted);
				param_link = parameter_link_node.child;
			}
			if (unchanged) {
				rebuilt = frame.id;
				break;
			}
			const CanonicalTypeNode return_node = nodeUnlocked(substituted_return);
			if (return_node.kind == CanonicalTypeKind::Function ||
				isInternalLink(return_node.kind) ||
				return_node.kind == CanonicalTypeKind::Array) {
				throw InternalError("canonical type: substituted invalid function return type");
			}
			TypeId rebuilt_param_link{};
			for (size_t index = rebuilt_args.size(); index-- > 0;) {
				const TypeId parameter = rebuilt_args[index];
				const CanonicalTypeNode parameter_node = nodeUnlocked(parameter);
				if (parameter_node.kind == CanonicalTypeKind::Function ||
					isInternalLink(parameter_node.kind) ||
					parameter_node.kind == CanonicalTypeKind::Array) {
					throw InternalError("canonical type: substituted undecayed function parameter type");
				}
				rebuilt_param_link = internUnlocked({
					.child = rebuilt_param_link,
					.kind = CanonicalTypeKind::FunctionParam,
					.builtin = CanonicalBuiltinKind::Void,
					.qualifiers = CVQualifier::None,
					.flags = CanonicalTypeNodeFlags::None,
					.array_extent = parameter.value,
				});
			}
			rebuilt = internUnlocked({
				.child = substituted_return,
				.kind = CanonicalTypeKind::Function,
				.builtin = node.builtin,
				.qualifiers = node.qualifiers,
				.flags = node.flags,
				.array_extent = packFunctionArrayExtent(
					rebuilt_param_link, unpackFunctionDependentNoexcept(node.array_extent)),
			});
			break;
		}
		case CanonicalTypeKind::MemberObjectPointer:
		case CanonicalTypeKind::MemberFunctionPointer: {
			const TypeId original_owner = TypeId{static_cast<uint32_t>(node.array_extent)};
			const TypeId substituted_owner = memo.at(original_owner.value);
			const TypeId substituted_pointee = memo.at(node.child.value);
			if (substituted_owner == original_owner && substituted_pointee == node.child) {
				rebuilt = frame.id;
				break;
			}
			if (nodeUnlocked(substituted_owner).kind != CanonicalTypeKind::Record) {
				throw InternalError("canonical type: substituted member pointer owner must be a record");
			}
			if (node.kind == CanonicalTypeKind::MemberFunctionPointer) {
				if (nodeUnlocked(substituted_pointee).kind != CanonicalTypeKind::Function) {
					throw InternalError("canonical type: substituted member function pointee must be a function");
				}
			} else {
				const CanonicalTypeNode pointee_node = nodeUnlocked(substituted_pointee);
				if (pointee_node.kind == CanonicalTypeKind::Function ||
					isInternalLink(pointee_node.kind) ||
					(pointee_node.kind == CanonicalTypeKind::Builtin &&
						pointee_node.builtin == CanonicalBuiltinKind::Void)) {
					throw InternalError("canonical type: substituted invalid member object pointee");
				}
			}
			rebuilt = internUnlocked({
				.child = substituted_pointee,
				.kind = node.kind,
				.builtin = CanonicalBuiltinKind::Void,
				.qualifiers = CVQualifier::None,
				.flags = CanonicalTypeNodeFlags::None,
				.array_extent = substituted_owner.value,
			});
			break;
		}
		default:
			throw InternalError("canonical type: unexpected substitute build kind");
		}
		memo.emplace(frame.id.value, rebuilt);
	}
	return memo.at(type.value);
}

bool CanonicalTypeTable::isMemberPointer(CanonicalTypeKind kind) {
	return kind == CanonicalTypeKind::MemberObjectPointer ||
		kind == CanonicalTypeKind::MemberFunctionPointer;
}

TypeId CanonicalTypeTable::recordOwnerUnlocked(TypeId owner) const {
	CanonicalTypeNode input = nodeUnlocked(owner);
	if (input.kind == CanonicalTypeKind::Qualified) {
		owner = input.child;
		input = nodeUnlocked(owner);
	}
	if (input.kind != CanonicalTypeKind::Record) {
		throw InternalError("canonical type: member pointer owner must be a record");
	}
	return owner;
}

TypeId CanonicalTypeTable::arrayUnlocked(TypeId element, uint64_t extent, CanonicalTypeNodeFlags flags) {
	CanonicalTypeNode element_node = nodeUnlocked(element);
	if (element_node.kind == CanonicalTypeKind::Qualified) {
		element_node = nodeUnlocked(element_node.child);
	}
	if (isReference(element_node.kind) ||
		element_node.kind == CanonicalTypeKind::Function ||
		isInternalLink(element_node.kind) ||
		(element_node.kind == CanonicalTypeKind::Builtin && element_node.builtin == CanonicalBuiltinKind::Void) ||
		(element_node.kind == CanonicalTypeKind::Array && element_node.flags != CanonicalTypeNodeFlags::KnownArrayBound)) {
		throw InternalError("canonical type: invalid array element type");
	}
	return internUnlocked({
		.child = element,
		.kind = CanonicalTypeKind::Array,
		.builtin = CanonicalBuiltinKind::Void,
		.qualifiers = CVQualifier::None,
		.flags = flags,
		.array_extent = extent,
	});
}

CanonicalTypeNode CanonicalTypeTable::nodeUnlocked(TypeId id) const {
	if (!id || id.value > live_count_) {
		throw InternalError("canonical type: TypeId is outside this table");
	}
	return nodes_[id.value - 1];
}

TypeId CanonicalTypeTable::internUnlocked(CanonicalTypeNode node) {
	traceRequestUnlocked(node);
	const auto existing = ids_.find(node);
	if (existing != ids_.end()) {
		return existing->second;
	}
	if (live_count_ >= std::numeric_limits<uint32_t>::max()) {
		throw InternalError("canonical type: TypeId space exhausted");
	}
	const TypeId id{static_cast<uint32_t>(live_count_ + 1)};
	if (live_count_ == nodes_.size()) {
		nodes_.push_back(node);
	} else {
		// Reuse discarded slots; repeated failed probes must not repeatedly
		// reserve new chunks from ChunkedVector's monotonic allocator.
		nodes_[live_count_] = node;
	}
	++live_count_;
	noteArenaBytes();
	try {
		ids_.emplace(node, id);
	} catch (...) {
		--live_count_;
		noteArenaBytes();
		throw;
	}
	return id;
}

void CanonicalTypeTable::validateRecordMemberUnlocked(const CanonicalRecordMember& member) const {
	if (isInternalLink(nodeUnlocked(member.type).kind)) {
		throw InternalError("canonical type: internal link is not a record member type");
	}
	const bool is_bitfield =
		hasCanonicalRecordMemberFlag(member.flags, CanonicalRecordMemberFlags::Bitfield);
	// Zero-width bitfields (C++ layout alignment directives) keep the Bitfield
	// flag with bit_width == 0. Only non-bitfields must not carry widths/offsets.
	if (!is_bitfield && (member.bit_width != 0 || member.bit_offset != 0)) {
		throw InternalError("canonical type: non-bitfield member has bitfield fields");
	}
}

void CanonicalTypeTable::validateRecordBaseUnlocked(const CanonicalRecordBase& base) const {
	if (!base.entity) {
		throw InternalError("canonical type: record base requires EntityId");
	}
}

bool CanonicalTypeTable::nameBytesEqualUnlocked(TypeId name_link, std::string_view identifier) const {
	size_t offset = 0;
	TypeId cursor = name_link;
	while (cursor) {
		const CanonicalTypeNode bytes = nodeUnlocked(cursor);
		if (bytes.kind != CanonicalTypeKind::NameBytes) {
			throw InternalError("canonical type: expected NameBytes link");
		}
		const uint8_t count = static_cast<uint8_t>(bytes.builtin);
		if (offset + count > identifier.size()) {
			return false;
		}
		for (uint8_t index = 0; index < count; ++index) {
			const unsigned char stored =
				static_cast<unsigned char>((bytes.array_extent >> (index * 8)) & 0xff);
			if (stored != static_cast<unsigned char>(identifier[offset + index])) {
				return false;
			}
		}
		offset += count;
		cursor = bytes.child;
	}
	return offset == identifier.size();
}

std::optional<TypeId> CanonicalTypeTable::tryLookupNamedTypeMemberUnlocked(EntityId entity,
	std::string_view name) const {
	if (!entity || name.empty() || name.find('\0') != std::string_view::npos) {
		return std::nullopt;
	}
	const auto found = named_type_member_schema_ids_.find(entity.value);
	if (found == named_type_member_schema_ids_.end()) {
		return std::nullopt;
	}
	const CanonicalNamedTypeMemberSchemaHeader header =
		named_type_member_schema_headers_[found->second];
	for (uint16_t index = 0; index < header.member_count; ++index) {
		const CanonicalNamedTypeMember& member =
			named_type_members_[header.member_begin + index];
		if (nameBytesEqualUnlocked(member.name, name)) {
			return member.type;
		}
	}
	return std::nullopt;
}

std::optional<TypeId> CanonicalTypeTable::resolveMemberAliasTargetUnlocked(TemplateDeclId member,
	TypeId owner_specialization, std::span<const TypeId> alias_args) {
	const auto published = alias_template_targets_.find(member.value);
	if (published == alias_template_targets_.end() || !published->second.owner ||
		published->second.parameter_kinds.size() != alias_args.size()) {
		return std::nullopt;
	}
	const CanonicalTypeNode owner_node = nodeUnlocked(owner_specialization);
	if (owner_node.kind != CanonicalTypeKind::TemplateSpecialization ||
		owner_node.array_extent != published->second.owner.value) {
		return std::nullopt;
	}
	// Owner arguments must be type-only and concrete; a NonType or
	// template-template link has no directly representable member target.
	TemplateVector<CanonicalTemplateArgument, 4> owner_arguments;
	for (TypeId link = owner_node.child; link; link = nodeUnlocked(link).child) {
		const CanonicalTypeNode argument = nodeUnlocked(link);
		if (argument.kind != CanonicalTypeKind::TemplateArg) {
			return std::nullopt;
		}
		const TypeId value{static_cast<uint32_t>(argument.array_extent)};
		if (isDependentAliasArgumentUnlocked(value) ||
			isInternalLink(nodeUnlocked(value).kind)) {
			return std::nullopt;
		}
		owner_arguments.push_back(CanonicalTemplateArgument::makeType(value));
	}
	// Member arguments must match a type-only published layout and be
	// concrete; every other published kind keeps the member boundary.
	TemplateVector<CanonicalTemplateArgument, 4> member_arguments;
	for (size_t index = 0; index < alias_args.size(); ++index) {
		if (published->second.parameter_kinds[index] != CanonicalTemplateArgKind::Type ||
			isDependentAliasArgumentUnlocked(alias_args[index]) ||
			isInternalLink(nodeUnlocked(alias_args[index]).kind)) {
			return std::nullopt;
		}
		member_arguments.push_back(CanonicalTemplateArgument::makeType(alias_args[index]));
	}
	if (!templateParameterReferencesCoveredUnlocked(published->second.target,
			published->second.owner, owner_arguments.size()) ||
		!templateParameterReferencesCoveredUnlocked(published->second.target,
			member, member_arguments.size())) {
		return std::nullopt;
	}
	const TypeId owner_target = substituteArgumentsUnlocked(published->second.target,
		published->second.owner, owner_arguments);
	return resolveNestedAliasGraphUnlocked(substituteArgumentsUnlocked(
		owner_target, member, member_arguments));
}

std::optional<TypeId> CanonicalTypeTable::resolveMemberAliasUseUnlocked(TypeId use) {
	const CanonicalTypeNode node = nodeUnlocked(use);
	if (node.kind != CanonicalTypeKind::DependentMemberAlias) {
		return std::nullopt;
	}
	const TemplateDeclId member = unpackDependentMemberAliasDecl(node.array_extent);
	TemplateVector<TypeId, 4> alias_args;
	for (TypeId link = unpackDependentMemberAliasArgs(node.array_extent); link;
		 link = nodeUnlocked(link).child) {
		const CanonicalTypeNode argument = nodeUnlocked(link);
		if (argument.kind != CanonicalTypeKind::TemplateArg) {
			return std::nullopt;
		}
		alias_args.push_back(TypeId{static_cast<uint32_t>(argument.array_extent)});
	}
	return resolveMemberAliasTargetUnlocked(
		member, node.child, std::span<const TypeId>(alias_args.data(), alias_args.size()));
}

TypeId CanonicalTypeTable::tryResolveDependentTipUnlocked(TypeId type) {
	if (!type) {
		throw InternalError("canonical type: invalid dependent tip TypeId");
	}
	if (nodeUnlocked(type).kind == CanonicalTypeKind::DependentMemberAlias) {
		const std::optional<TypeId> resolved = resolveMemberAliasUseUnlocked(type);
		return resolved.has_value() ? *resolved : type;
	}
	if (nodeUnlocked(type).kind != CanonicalTypeKind::DependentName) {
		return type;
	}
	std::vector<TypeId> name_links;
	TypeId cursor = type;
	while (nodeUnlocked(cursor).kind == CanonicalTypeKind::DependentName) {
		const CanonicalTypeNode node = nodeUnlocked(cursor);
		name_links.push_back(TypeId{static_cast<uint32_t>(node.array_extent)});
		cursor = node.child;
	}
	if (nodeUnlocked(cursor).kind != CanonicalTypeKind::Record) {
		return type;
	}
	TypeId current = cursor;
	for (size_t index = name_links.size(); index-- > 0;) {
		if (nodeUnlocked(current).kind != CanonicalTypeKind::Record) {
			return type;
		}
		const EntityId entity = EntityId{static_cast<uint32_t>(
			nodeUnlocked(current).array_extent)};
		const auto found = named_type_member_schema_ids_.find(entity.value);
		if (found == named_type_member_schema_ids_.end()) {
			return type;
		}
		const CanonicalNamedTypeMemberSchemaHeader header =
			named_type_member_schema_headers_[found->second];
		std::optional<TypeId> matched;
		for (uint16_t member_index = 0; member_index < header.member_count; ++member_index) {
			const CanonicalNamedTypeMember& member =
				named_type_members_[header.member_begin + member_index];
			if (member.name == name_links[index]) {
				matched = member.type;
				break;
			}
		}
		if (!matched.has_value()) {
			return type;
		}
		current = *matched;
	}
	return current;
}

uint64_t CanonicalTypeTable::usedBytesUnlocked() const {
	return static_cast<uint64_t>(live_count_) * sizeof(CanonicalTypeNode) +
		static_cast<uint64_t>(live_record_layout_count_) * sizeof(CanonicalRecordLayout) +
		static_cast<uint64_t>(live_enum_layout_count_) * sizeof(CanonicalEnumLayout) +
		static_cast<uint64_t>(live_record_field_schema_count_) *
			sizeof(CanonicalRecordFieldSchemaHeader) +
		static_cast<uint64_t>(live_record_member_count_) * sizeof(CanonicalRecordMember) +
		static_cast<uint64_t>(live_record_base_count_) * sizeof(CanonicalRecordBase) +
		static_cast<uint64_t>(live_named_type_member_schema_count_) *
			sizeof(CanonicalNamedTypeMemberSchemaHeader) +
		static_cast<uint64_t>(live_named_type_member_count_) * sizeof(CanonicalNamedTypeMember);
}

uint64_t CanonicalTypeTable::reservedBytesUnlocked() const {
	return nodes_.reservedBytes() + record_layouts_.reservedBytes() +
		enum_layouts_.reservedBytes() + record_field_schema_headers_.reservedBytes() +
		record_members_.reservedBytes() + record_bases_.reservedBytes() +
		named_type_member_schema_headers_.reservedBytes() +
		named_type_members_.reservedBytes();
}

void CanonicalTypeTable::noteArenaBytes() {
	if (accounting_ != nullptr) {
		accounting_->update(SemanticArenaComponent::Types, usedBytesUnlocked(), reservedBytesUnlocked());
	}
}

void CanonicalTypeTable::checkTransactionThread() const {
	if (!transaction_marks_.empty() && transaction_owner_ != std::this_thread::get_id()) {
		throw InternalError("canonical type transaction belongs to another thread");
	}
}

size_t CanonicalTypeTable::beginTransaction() {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	transaction_marks_.push_back({
		live_count_,
		live_record_layout_count_,
		live_enum_layout_count_,
		live_record_field_schema_count_,
		live_record_member_count_,
		live_record_base_count_,
		live_named_type_member_schema_count_,
		live_named_type_member_count_,
	});
	transaction_owner_ = std::this_thread::get_id();
	return transaction_marks_.size();
}

void CanonicalTypeTable::finishTransaction(size_t depth, bool commit) {
	std::lock_guard lock(mutex_);
	checkTransactionThread();
	if (depth == 0 || depth != transaction_marks_.size()) {
		throw InternalError("canonical type transactions must finish in nesting order");
	}
	if (!commit) {
		const TransactionMark mark = transaction_marks_.back();
		while (live_count_ > mark.node_count) {
			ids_.erase(nodes_[live_count_ - 1]);
			--live_count_;
		}
		while (live_record_layout_count_ > mark.record_layout_count) {
			record_layout_ids_.erase(record_layouts_[live_record_layout_count_ - 1].entity.value);
			--live_record_layout_count_;
		}
		while (live_enum_layout_count_ > mark.enum_layout_count) {
			enum_layout_ids_.erase(enum_layouts_[live_enum_layout_count_ - 1].entity.value);
			--live_enum_layout_count_;
		}
		while (live_record_field_schema_count_ > mark.record_field_schema_count) {
			record_field_schema_ids_.erase(
				record_field_schema_headers_[live_record_field_schema_count_ - 1].entity.value);
			--live_record_field_schema_count_;
		}
		live_record_member_count_ = mark.record_member_count;
		live_record_base_count_ = mark.record_base_count;
		while (live_named_type_member_schema_count_ > mark.named_type_member_schema_count) {
			named_type_member_schema_ids_.erase(
				named_type_member_schema_headers_[live_named_type_member_schema_count_ - 1]
					.entity.value);
			--live_named_type_member_schema_count_;
		}
		live_named_type_member_count_ = mark.named_type_member_count;
		noteArenaBytes();
	}
	transaction_marks_.pop_back();
}

void CanonicalTypeTable::appendNodeTraceFields(StringBuilder& shape, CanonicalTypeNode node, uint64_t extent) const {
	shape.append(static_cast<uint64_t>(node.kind)).append(',');
	shape.append(static_cast<uint64_t>(node.builtin)).append(',');
	shape.append(static_cast<uint64_t>(node.qualifiers)).append(',');
	shape.append(static_cast<uint64_t>(node.flags)).append(',');
	shape.append(extent);
}

void CanonicalTypeTable::appendTypeIdTrace(StringBuilder& shape, TypeId id) const {
	CanonicalTypeNode current = nodeUnlocked(id);
	for (;;) {
		if (isDependentNameFamily(current.kind)) {
			appendDependentNameFamilyTrace(shape, current);
			shape.append('/');
			current = nodeUnlocked(current.child);
			continue;
		}
		if (current.kind == CanonicalTypeKind::Function) {
			const TypeId param_link = unpackFunctionParamLink(current.array_extent);
			const ExprId dependent = unpackFunctionDependentNoexcept(current.array_extent);
			appendNodeTraceFields(
				shape,
				current,
				(param_link ? 1ull : 0ull) | (static_cast<uint64_t>(dependent.value) << 1));
			TypeId cursor = param_link;
			while (cursor) {
				const CanonicalTypeNode param = nodeUnlocked(cursor);
				shape.append('/');
				appendNodeTraceFields(shape, param, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(param.array_extent)});
				cursor = param.child;
			}
			shape.append('/');
			current = nodeUnlocked(current.child);
			continue;
		}
		if (current.kind == CanonicalTypeKind::TemplateSpecialization) {
			appendNodeTraceFields(shape, current, current.array_extent);
			TypeId cursor = current.child;
			while (cursor) {
				const CanonicalTypeNode arg = nodeUnlocked(cursor);
				shape.append('/');
				if (arg.kind == CanonicalTypeKind::NonTypeTemplateArg ||
					arg.kind == CanonicalTypeKind::TemplateTemplateArg ||
					arg.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
					appendNodeTraceFields(shape, arg, arg.array_extent);
				} else {
					appendNodeTraceFields(shape, arg, 0);
					shape.append('/');
					appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
				}
				cursor = arg.child;
			}
			break;
		}
		if (isMemberPointer(current.kind)) {
			appendNodeTraceFields(shape, current, 0);
			shape.append('/');
			appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(current.array_extent)});
			shape.append('/');
			current = nodeUnlocked(current.child);
			continue;
		}
		appendNodeTraceFields(shape, current,
			current.kind == CanonicalTypeKind::FunctionParam ||
				current.kind == CanonicalTypeKind::TemplateArg ||
				current.kind == CanonicalTypeKind::NonTypeTemplateArg ||
				current.kind == CanonicalTypeKind::TemplateTemplateArg ||
				current.kind == CanonicalTypeKind::DependentTemplateTemplateArg
				? ((current.kind == CanonicalTypeKind::NonTypeTemplateArg ||
					current.kind == CanonicalTypeKind::TemplateTemplateArg ||
					current.kind == CanonicalTypeKind::DependentTemplateTemplateArg) ? current.array_extent : 0)
				: current.array_extent);
		if (!current.child || current.kind == CanonicalTypeKind::Builtin ||
			current.kind == CanonicalTypeKind::FunctionParam ||
			current.kind == CanonicalTypeKind::TemplateArg ||
			current.kind == CanonicalTypeKind::NonTypeTemplateArg ||
			current.kind == CanonicalTypeKind::TemplateTemplateArg ||
			current.kind == CanonicalTypeKind::DependentTemplateTemplateArg ||
			current.kind == CanonicalTypeKind::Record ||
			current.kind == CanonicalTypeKind::TemplateParameter) {
			break;
		}
		shape.append('/');
		current = nodeUnlocked(current.child);
	}
}

void CanonicalTypeTable::appendDependentNameFamilyTrace(StringBuilder& shape, CanonicalTypeNode node) const {
	appendNodeTraceFields(shape, node, 0);
	TypeId name_link = node.kind == CanonicalTypeKind::DependentTemplateMember
		? unpackDependentTemplateMemberName(node.array_extent)
		: TypeId{static_cast<uint32_t>(node.array_extent)};
	TypeId cursor = name_link;
	while (cursor) {
		const auto bytes = nodeUnlocked(cursor);
		shape.append('/');
		appendNodeTraceFields(shape, bytes, bytes.array_extent);
		cursor = bytes.child;
	}
	if (node.kind == CanonicalTypeKind::DependentTemplateMember) {
		TypeId arg_cursor = unpackDependentTemplateMemberArgs(node.array_extent);
		while (arg_cursor) {
			const CanonicalTypeNode arg = nodeUnlocked(arg_cursor);
			shape.append('/');
			appendNodeTraceFields(shape, arg, 0);
			shape.append('/');
			appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
			arg_cursor = arg.child;
		}
	}
}

void CanonicalTypeTable::appendNodeTrace(StringBuilder& shape, CanonicalTypeNode node) const {
	if (isDependentNameFamily(node.kind)) {
		appendDependentNameFamilyTrace(shape, node);
		shape.append('/');
		appendTypeIdTrace(shape, node.child);
		return;
	}
	if (node.kind == CanonicalTypeKind::Function) {
		const TypeId param_link = unpackFunctionParamLink(node.array_extent);
		const ExprId dependent = unpackFunctionDependentNoexcept(node.array_extent);
		appendNodeTraceFields(
			shape,
			node,
			(param_link ? 1ull : 0ull) | (static_cast<uint64_t>(dependent.value) << 1));
		TypeId cursor = param_link;
		while (cursor) {
			const CanonicalTypeNode param = nodeUnlocked(cursor);
			shape.append('/');
			appendNodeTraceFields(shape, param, 0);
			shape.append('/');
			appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(param.array_extent)});
			cursor = param.child;
		}
		shape.append('/');
		appendTypeIdTrace(shape, node.child);
		return;
	}
	if (node.kind == CanonicalTypeKind::TemplateSpecialization) {
		appendNodeTraceFields(shape, node, node.array_extent);
		TypeId cursor = node.child;
		while (cursor) {
			const CanonicalTypeNode arg = nodeUnlocked(cursor);
			shape.append('/');
			if (arg.kind == CanonicalTypeKind::NonTypeTemplateArg ||
				arg.kind == CanonicalTypeKind::TemplateTemplateArg ||
				arg.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
				appendNodeTraceFields(shape, arg, arg.array_extent);
			} else {
				appendNodeTraceFields(shape, arg, 0);
				shape.append('/');
				appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(arg.array_extent)});
			}
			cursor = arg.child;
		}
		return;
	}
	if (node.kind == CanonicalTypeKind::FunctionParam ||
		node.kind == CanonicalTypeKind::TemplateArg) {
		appendNodeTraceFields(shape, node, 0);
		shape.append('/');
		appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(node.array_extent)});
		return;
	}
	if (node.kind == CanonicalTypeKind::NonTypeTemplateArg ||
		node.kind == CanonicalTypeKind::TemplateTemplateArg ||
		node.kind == CanonicalTypeKind::DependentTemplateTemplateArg) {
		appendNodeTraceFields(shape, node, node.array_extent);
		return;
	}
	if (isMemberPointer(node.kind)) {
		appendNodeTraceFields(shape, node, 0);
		shape.append('/');
		appendTypeIdTrace(shape, TypeId{static_cast<uint32_t>(node.array_extent)});
		shape.append('/');
		appendTypeIdTrace(shape, node.child);
		return;
	}
	appendNodeTraceFields(shape, node, node.array_extent);
	if (!node.child || node.kind == CanonicalTypeKind::Builtin ||
		node.kind == CanonicalTypeKind::Record ||
		node.kind == CanonicalTypeKind::TemplateParameter) {
		return;
	}
	shape.append('/');
	appendTypeIdTrace(shape, node.child);
}

void CanonicalTypeTable::traceRequestUnlocked(CanonicalTypeNode node) const {
	if (!FLASH_LOG_ENABLED(Types, Trace)) {
		return;
	}
	// Trace-local structural spelling only: never serialize numeric TypeIds.
	// Each slash-separated node is kind,builtin,cv,flags,extent. Function
	// and member-pointer owner/param TypeIds expand as nested shapes.
	// Record extent carries EntityId, which is entity identity rather than a
	// TypeId slot.
	StringBuilder shape;
	appendNodeTrace(shape, node);
	FLASH_LOG(Types, Trace, "canonical-request-v2 ", shape.commit());
}
