#include "DeclarationBuilder.h"

#include "AstNodeTypes.h"
#include "CompileError.h"
#include "SymbolTable.h"

PublishResult DeclarationBuilder::makeRejected(EntityId existing_entity) {
	return PublishResult{PublishStatus::Rejected, DeclId{}, existing_entity};
}

uint8_t DeclarationBuilder::requestFlags(const FunctionDeclRequest& request) {
	uint8_t flags = 0;
	if (request.is_definition) {
		flags = static_cast<uint8_t>(flags | DeclarationFlags::IsDefinition);
	}
	if (request.is_inline || request.is_constexpr) {
		flags = static_cast<uint8_t>(flags | DeclarationFlags::IsInline);
	}
	if (request.is_constexpr) {
		flags = static_cast<uint8_t>(flags | DeclarationFlags::IsConstexpr);
	}
	return flags;
}

bool DeclarationBuilder::hasFlag(uint8_t flags, uint8_t bit) {
	return (flags & bit) != 0;
}

bool DeclarationBuilder::isPublishableScopeType(ScopeType scope_type) {
	return scope_type == ScopeType::Global || scope_type == ScopeType::Namespace;
}

std::optional<DeclarationBuilder::PublicationTarget> DeclarationBuilder::resolvePublicationTarget(
	const SymbolTable& symbol_table,
	ScopeId lexical_scope_id) {
	if (!lexical_scope_id) {
		return std::nullopt;
	}

	const Scope* scope = symbol_table.findScopeById(lexical_scope_id);
	if (scope == nullptr) {
		return std::nullopt;
	}
	if (!isPublishableScopeType(scope->scope_type)) {
		return std::nullopt;
	}

	OwnerId owner_id{};
	if (scope->scope_type == ScopeType::Global) {
		owner_id = ownerIdFromNamespaceHandle(NamespaceRegistry::GLOBAL_NAMESPACE);
	} else {
		owner_id = ownerIdFromNamespaceHandle(scope->namespace_handle);
	}
	if (!owner_id) {
		return std::nullopt;
	}

	return PublicationTarget{lexical_scope_id, owner_id};
}

bool DeclarationBuilder::isValidRequest(const FunctionDeclRequest& request) const {
	if (!request.lexical_scope_id) {
		return false;
	}
	if (!request.name.isValid()) {
		return false;
	}
	if (!request.signature_id) {
		return false;
	}
	if (!request.return_type_id) {
		return false;
	}
	if (request.language_linkage != LanguageLinkage::CPlusPlus) {
		return false;
	}
	return true;
}

DeclId DeclarationBuilder::allocateDeclaration(DeclarationRecord record) {
	if (declarations_.size() >= 0xffffffffu) {
		throw InternalError("DeclarationBuilder: DeclId space exhausted");
	}
	const DeclId id{static_cast<uint32_t>(declarations_.size() + 1)};
	record.id = id;
	declarations_.push_back(record);
	return id;
}

EntityId DeclarationBuilder::allocateEntity(EntityRecord record) {
	if (entities_.size() >= 0xffffffffu) {
		throw InternalError("DeclarationBuilder: EntityId space exhausted");
	}
	const EntityId id{static_cast<uint32_t>(entities_.size() + 1)};
	record.id = id;
	entities_.push_back(record);
	return id;
}

const DeclarationRecord& DeclarationBuilder::declaration(DeclId decl_id) const {
	if (!decl_id || decl_id.value > declarations_.size()) {
		throw InternalError("DeclarationBuilder: invalid DeclId");
	}
	return declarations_[decl_id.value - 1];
}

const EntityRecord& DeclarationBuilder::entity(EntityId entity_id) const {
	if (!entity_id || entity_id.value > entities_.size()) {
		throw InternalError("DeclarationBuilder: invalid EntityId");
	}
	return entities_[entity_id.value - 1];
}

DeclarationBuilder::Checkpoint DeclarationBuilder::mark() const {
	Checkpoint checkpoint;
	checkpoint.declaration_count = declarations_.size();
	checkpoint.entity_count = entities_.size();
	checkpoint.entities.reserve(checkpoint.entity_count);
	for (std::size_t index = 0; index < entities_.size(); ++index) {
		checkpoint.entities.push_back(entities_[index]);
	}
	checkpoint.declarator_types = declarator_type_canon_;
	checkpoint.parameter_lists = parameter_list_ids_;
	return checkpoint;
}

void DeclarationBuilder::rebuildEntityLookupFromEntities() {
	entity_by_key_.clear();
	for (const EntityRecord& entity : entities_) {
		const EntityLookupKey key{
			entity.owner_id.value,
			entity.name.handle,
			entity.signature_id.value};
		entity_by_key_.emplace(key, entity.id);
	}
}

void DeclarationBuilder::rollbackTo(const Checkpoint& checkpoint) {
	while (declarations_.size() > checkpoint.declaration_count) {
		declarations_.pop_back();
	}
	while (entities_.size() > checkpoint.entity_count) {
		entities_.pop_back();
	}
	for (std::size_t index = 0; index < checkpoint.entity_count; ++index) {
		entities_[index] = checkpoint.entities[index];
	}
	declarator_type_canon_ = checkpoint.declarator_types;
	parameter_list_ids_ = checkpoint.parameter_lists;
	rebuildEntityLookupFromEntities();
}

PublishResult DeclarationBuilder::classifyPublishFunction(
	const FunctionDeclRequest& request,
	const SymbolTable& symbol_table) const {
	if (!isValidRequest(request)) {
		return makeRejected(EntityId{});
	}

	const std::optional<PublicationTarget> target =
		resolvePublicationTarget(symbol_table, request.lexical_scope_id);
	if (!target.has_value()) {
		return makeRejected(EntityId{});
	}

	const EntityLookupKey key{
		target->owner_id.value,
		request.name.handle,
		request.signature_id.value};

	const uint8_t incoming_flags = requestFlags(request);
	const auto existing = entity_by_key_.find(key);
	if (existing == entity_by_key_.end()) {
		return PublishResult{PublishStatus::Created, DeclId{}, EntityId{}};
	}

	const EntityRecord& live_entity = entities_[existing->second.value - 1];
	const EntityId entity_id = live_entity.id;

	if (live_entity.return_type_id != request.return_type_id) {
		return makeRejected(entity_id);
	}

	const bool prior_constexpr = hasFlag(live_entity.flags, DeclarationFlags::IsConstexpr);
	if (prior_constexpr != request.is_constexpr) {
		return makeRejected(entity_id);
	}

	const bool prior_definition = hasFlag(live_entity.flags, DeclarationFlags::IsDefinition);
	if (request.is_definition && prior_definition) {
		return makeRejected(entity_id);
	}

	const bool prior_inline = hasFlag(live_entity.flags, DeclarationFlags::IsInline);
	if (request.is_inline && prior_definition && !prior_inline) {
		return makeRejected(entity_id);
	}

	return PublishResult{PublishStatus::MergedRedeclaration, DeclId{}, entity_id};
}

PublishResult DeclarationBuilder::publishFunction(
	const FunctionDeclRequest& request,
	const SymbolTable& symbol_table) {
	const PublishResult classification = classifyPublishFunction(request, symbol_table);
	if (classification.status == PublishStatus::Rejected) {
		return classification;
	}
	return commitClassifiedPublishFunction(classification, request, symbol_table);
}

PublishResult DeclarationBuilder::commitClassifiedPublishFunction(
	const PublishResult& classification,
	const FunctionDeclRequest& request,
	const SymbolTable& symbol_table) {
	if (classification.status == PublishStatus::Rejected) {
		return classification;
	}

	const std::optional<PublicationTarget> target =
		resolvePublicationTarget(symbol_table, request.lexical_scope_id);
	if (!target.has_value()) {
		return makeRejected(EntityId{});
	}

	const EntityLookupKey key{
		target->owner_id.value,
		request.name.handle,
		request.signature_id.value};

	const uint8_t incoming_flags = requestFlags(request);
	if (classification.status == PublishStatus::Created) {
		EntityRecord entity_record{};
		entity_record.owner_id = target->owner_id;
		entity_record.name = request.name;
		entity_record.signature_id = request.signature_id;
		entity_record.return_type_id = request.return_type_id;
		entity_record.kind = static_cast<uint8_t>(DeclKind::Function);
		entity_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
		entity_record.flags = incoming_flags;
		entity_record.reserved = 0;

		const EntityId entity_id = allocateEntity(entity_record);

		DeclarationRecord decl_record{};
		decl_record.entity_id = entity_id;
		decl_record.previous_decl_id = DeclId{};
		decl_record.lexical_scope_id = request.lexical_scope_id;
		decl_record.name = request.name;
		decl_record.signature_id = request.signature_id;
		decl_record.return_type_id = request.return_type_id;
		decl_record.kind = static_cast<uint8_t>(DeclKind::Function);
		decl_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
		decl_record.flags = incoming_flags;
		decl_record.reserved = 0;

		DeclId decl_id{};
		try {
			decl_id = allocateDeclaration(decl_record);
			EntityRecord& live_entity = entities_[entity_id.value - 1];
			live_entity.first_decl_id = decl_id;
			live_entity.latest_decl_id = decl_id;
			entity_by_key_.emplace(key, entity_id);
		} catch (...) {
			if (decl_id) {
				declarations_.pop_back();
			}
			entities_.pop_back();
			throw;
		}

		return PublishResult{PublishStatus::Created, decl_id, entity_id};
	}

	EntityRecord& live_entity = entities_[classification.entity_id.value - 1];
	const EntityId entity_id = live_entity.id;

	DeclarationRecord decl_record{};
	decl_record.entity_id = entity_id;
	decl_record.previous_decl_id = live_entity.latest_decl_id;
	decl_record.lexical_scope_id = request.lexical_scope_id;
	decl_record.name = request.name;
	decl_record.signature_id = request.signature_id;
	decl_record.return_type_id = request.return_type_id;
	decl_record.kind = static_cast<uint8_t>(DeclKind::Function);
	decl_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
	decl_record.flags = incoming_flags;
	decl_record.reserved = 0;

	const DeclId decl_id = allocateDeclaration(decl_record);
	live_entity.latest_decl_id = decl_id;
	if (request.is_definition) {
		live_entity.flags = static_cast<uint8_t>(live_entity.flags | DeclarationFlags::IsDefinition);
	}
	if (request.is_inline || request.is_constexpr) {
		live_entity.flags = static_cast<uint8_t>(live_entity.flags | DeclarationFlags::IsInline);
	}

	return PublishResult{PublishStatus::MergedRedeclaration, decl_id, entity_id};
}

TypeId DeclarationBuilder::internDeclaratorType(const TypeSpecifierNode& type_spec) {
	for (std::size_t index = 0; index < declarator_type_canon_.size(); ++index) {
		if (declarator_type_canon_[index].matches_signature(type_spec)) {
			return TypeId{static_cast<uint32_t>(index + 1)};
		}
	}
	declarator_type_canon_.push_back(type_spec);
	return TypeId{static_cast<uint32_t>(declarator_type_canon_.size())};
}

TypeId DeclarationBuilder::internParameterListSignature(
	std::span<const ASTNode> parameter_nodes,
	bool is_variadic) {
	ParameterListKey key;
	key.is_variadic = is_variadic;
	key.param_type_ids.reserve(parameter_nodes.size());
	for (const ASTNode& parameter_node : parameter_nodes) {
		const TypeSpecifierNode& param_type = parameter_node.as<DeclarationNode>().type_specifier_node();
		key.param_type_ids.push_back(internDeclaratorType(param_type).value);
	}

	const auto existing = parameter_list_ids_.find(key);
	if (existing != parameter_list_ids_.end()) {
		return existing->second;
	}

	const TypeId signature_id{static_cast<uint32_t>(parameter_list_ids_.size() + 1)};
	parameter_list_ids_.emplace(std::move(key), signature_id);
	return signature_id;
}

FunctionDeclRequest buildFreeFunctionDeclRequest(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition) {
	FunctionDeclRequest request{};
	request.lexical_scope_id = lexical_scope_id;
	request.name = func_decl.decl_node().identifier_token().handle();
	request.signature_id = builder.internParameterListSignature(
		func_decl.parameter_nodes(),
		func_decl.is_variadic());
	request.return_type_id = builder.internDeclaratorType(func_decl.decl_node().type_specifier_node());
	request.language_linkage = LanguageLinkage::CPlusPlus;
	request.is_definition = is_definition;
	request.is_inline = func_decl.is_inline();
	request.is_constexpr = func_decl.is_constexpr();
	return request;
}

bool shouldPublishParserFreeFunction(const FunctionDeclarationNode& func_decl, ScopeType scope_type) {
	if (func_decl.is_member_function()) {
		return false;
	}
	if (func_decl.is_template_pattern()) {
		return false;
	}
	if (func_decl.linkage() == Linkage::C) {
		return false;
	}
	if (scope_type != ScopeType::Global && scope_type != ScopeType::Namespace) {
		return false;
	}
	return true;
}

PublishResult publishParserFreeFunction(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table) {
	const FunctionDeclRequest request =
		buildFreeFunctionDeclRequest(builder, func_decl, lexical_scope_id, is_definition);
	return builder.publishFunction(request, symbol_table);
}

PublicationTransaction::PublicationTransaction(DeclarationBuilder& builder)
	: builder_(builder)
	, checkpoint_(builder.mark()) {
}

PublicationTransaction::~PublicationTransaction() {
	if (!committed_ && !rolled_back_) {
		rollback();
	}
}

void PublicationTransaction::commit() {
	committed_ = true;
}

void PublicationTransaction::rollback() {
	builder_.rollbackTo(checkpoint_);
	rolled_back_ = true;
}

namespace {

PublishResult publicationTransactionFailure(EntityId entity_id) {
	return PublishResult{PublishStatus::Rejected, DeclId{}, entity_id};
}

} // namespace

PublishResult commitParserFreeFunctionPublication(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table) {
	PublicationTransaction transaction(builder);
	const FunctionDeclRequest request =
		buildFreeFunctionDeclRequest(builder, func_decl, lexical_scope_id, is_definition);
	const PublishResult classified = builder.classifyPublishFunction(request, symbol_table);
	if (classified.status == PublishStatus::Rejected) {
		transaction.rollback();
		return classified;
	}

	const PublishResult committed =
		builder.commitClassifiedPublishFunction(classified, request, symbol_table);
	if (committed.status == PublishStatus::Rejected) {
		transaction.rollback();
		return committed;
	}
	if (committed.status != classified.status) {
		transaction.rollback();
		return publicationTransactionFailure(classified.entity_id);
	}
	if (classified.status == PublishStatus::MergedRedeclaration &&
		committed.entity_id != classified.entity_id) {
		transaction.rollback();
		return publicationTransactionFailure(classified.entity_id);
	}

	transaction.commit();
	return committed;
}
