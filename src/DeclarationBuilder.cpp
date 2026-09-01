#include "DeclarationBuilder.h"

#include "AstNodeTypes.h"
#include "CompileError.h"
#include "SymbolTable.h"

DeclarationBuilder::DeclarationBuilder() = default;

DeclarationBuilder::~DeclarationBuilder() = default;

std::size_t DeclarationBuilder::telemetryDeclaratorInternCount() const {
	return declarator_type_canon_.size();
}

PreparedFunctionPublication::PreparedFunctionPublication(
	PublishStatus status,
	EntityId entity_id,
	ScopeId lexical_scope_id,
	OwnerId owner_id,
	StringHandle name,
	TypeId signature_id,
	TypeId return_type_id,
	uint8_t flags)
	: status_(status)
	, entity_id_(entity_id)
	, lexical_scope_id_(lexical_scope_id)
	, owner_id_(owner_id)
	, name_(name)
	, signature_id_(signature_id)
	, return_type_id_(return_type_id)
	, flags_(flags)
	, consumed_(0) {
}

PreparedFunctionPublication::PreparedFunctionPublication(PreparedFunctionPublication&& other) noexcept
	: status_(other.status_)
	, entity_id_(other.entity_id_)
	, lexical_scope_id_(other.lexical_scope_id_)
	, owner_id_(other.owner_id_)
	, name_(other.name_)
	, signature_id_(other.signature_id_)
	, return_type_id_(other.return_type_id_)
	, flags_(other.flags_)
	, consumed_(other.consumed_) {
	other.consumed_ = 1;
	other.status_ = PublishStatus::Rejected;
}

PreparedFunctionPublication& PreparedFunctionPublication::operator=(PreparedFunctionPublication&& other) noexcept {
	if (this == &other) {
		return *this;
	}
	status_ = other.status_;
	entity_id_ = other.entity_id_;
	lexical_scope_id_ = other.lexical_scope_id_;
	owner_id_ = other.owner_id_;
	name_ = other.name_;
	signature_id_ = other.signature_id_;
	return_type_id_ = other.return_type_id_;
	flags_ = other.flags_;
	consumed_ = other.consumed_;
	other.consumed_ = 1;
	other.status_ = PublishStatus::Rejected;
	return *this;
}

void PreparedFunctionPublication::consume() {
	if (consumed_ != 0) {
		throw InternalError("DeclarationBuilder: PreparedFunctionPublication already committed");
	}
	consumed_ = 1;
}

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

	const ScopeMetadataView metadata = readScopeMetadata(symbol_table, lexical_scope_id);
	if (!isPublishableScopeType(metadata.scope_type)) {
		return std::nullopt;
	}

	OwnerId owner_id{};
	if (metadata.scope_type == ScopeType::Global) {
		owner_id = ownerIdFromNamespaceHandle(NamespaceRegistry::GLOBAL_NAMESPACE);
	} else {
		owner_id = ownerIdFromNamespaceHandle(metadata.namespace_handle);
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
	noteSemanticArenaPeaks();
	return id;
}

EntityId DeclarationBuilder::allocateEntity(EntityRecord record) {
	if (entities_.size() >= 0xffffffffu) {
		throw InternalError("DeclarationBuilder: EntityId space exhausted");
	}
	const EntityId id{static_cast<uint32_t>(entities_.size() + 1)};
	record.id = id;
	entities_.push_back(record);
	noteSemanticArenaPeaks();
	return id;
}

void DeclarationBuilder::noteSemanticArenaPeaks() {
	const uint64_t used = declarations_.usedBytes() + entities_.usedBytes();
	const uint64_t reserved = declarations_.reservedBytes() + entities_.reservedBytes();
	if (used > peak_used_bytes_) {
		peak_used_bytes_ = used;
	}
	if (reserved > peak_reserved_bytes_) {
		peak_reserved_bytes_ = reserved;
	}
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

PreparedFunctionPublication DeclarationBuilder::prepareFunctionPublication(
	const FunctionDeclRequest& request,
	const SymbolTable& symbol_table) const {
	if (!isValidRequest(request)) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			EntityId{},
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	const std::optional<PublicationTarget> target =
		resolvePublicationTarget(symbol_table, request.lexical_scope_id);
	if (!target.has_value()) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			EntityId{},
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	const EntityLookupKey key{
		target->owner_id.value,
		request.name.handle,
		request.signature_id.value};

	const auto existing = entity_by_key_.find(key);
	if (existing == entity_by_key_.end()) {
		return PreparedFunctionPublication(
			PublishStatus::Created,
			EntityId{},
			request.lexical_scope_id,
			target->owner_id,
			request.name,
			request.signature_id,
			request.return_type_id,
			requestFlags(request));
	}

	const EntityRecord& live_entity = entities_[existing->second.value - 1];
	const EntityId entity_id = live_entity.id;

	if (live_entity.return_type_id != request.return_type_id) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			entity_id,
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	const bool prior_constexpr = hasFlag(live_entity.flags, DeclarationFlags::IsConstexpr);
	if (prior_constexpr != request.is_constexpr) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			entity_id,
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	const bool prior_definition = hasFlag(live_entity.flags, DeclarationFlags::IsDefinition);
	if (request.is_definition && prior_definition) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			entity_id,
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	const bool prior_inline = hasFlag(live_entity.flags, DeclarationFlags::IsInline);
	if (request.is_inline && prior_definition && !prior_inline) {
		return PreparedFunctionPublication(
			PublishStatus::Rejected,
			entity_id,
			ScopeId{},
			OwnerId{},
			StringHandle{},
			TypeId{},
			TypeId{},
			0);
	}

	return PreparedFunctionPublication(
		PublishStatus::MergedRedeclaration,
		entity_id,
		request.lexical_scope_id,
		target->owner_id,
		request.name,
		request.signature_id,
		request.return_type_id,
		requestFlags(request));
}

PublishResult DeclarationBuilder::commitFunctionPublication(
	PreparedFunctionPublication& prepared,
	PublicationTransaction& transaction) {
	prepared.consume();
	if (prepared.isRejected()) {
		return prepared.rejection();
	}

	const EntityLookupKey key{
		prepared.owner_id_.value,
		prepared.name_.handle,
		prepared.signature_id_.value};

	if (prepared.status_ == PublishStatus::Created) {
		if (entity_by_key_.find(key) != entity_by_key_.end()) {
			throw InternalError("DeclarationBuilder: prepared Created publication key already exists");
		}

		EntityRecord entity_record{};
		entity_record.owner_id = prepared.owner_id_;
		entity_record.name = prepared.name_;
		entity_record.signature_id = prepared.signature_id_;
		entity_record.return_type_id = prepared.return_type_id_;
		entity_record.kind = static_cast<uint8_t>(DeclKind::Function);
		entity_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
		entity_record.flags = prepared.flags_;
		entity_record.reserved = 0;

		const EntityId entity_id = allocateEntity(entity_record);

		DeclarationRecord decl_record{};
		decl_record.entity_id = entity_id;
		decl_record.previous_decl_id = DeclId{};
		decl_record.lexical_scope_id = prepared.lexical_scope_id_;
		decl_record.name = prepared.name_;
		decl_record.signature_id = prepared.signature_id_;
		decl_record.return_type_id = prepared.return_type_id_;
		decl_record.kind = static_cast<uint8_t>(DeclKind::Function);
		decl_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
		decl_record.flags = prepared.flags_;
		decl_record.reserved = 0;

		DeclId decl_id{};
		try {
			decl_id = allocateDeclaration(decl_record);
			EntityRecord& live_entity = entities_[entity_id.value - 1];
			live_entity.first_decl_id = decl_id;
			live_entity.latest_decl_id = decl_id;
			const auto insert_result = entity_by_key_.emplace(key, entity_id);
			if (!insert_result.second) {
				throw InternalError("DeclarationBuilder: entity lookup map rejected insertion");
			}
			transaction.noteEntityLookupInsert(key);
		} catch (...) {
			entity_by_key_.erase(key);
			if (decl_id) {
				declarations_.pop_back();
			}
			entities_.pop_back();
			throw;
		}

		return PublishResult{PublishStatus::Created, decl_id, entity_id};
	}

	if (!prepared.entity_id_ || prepared.entity_id_.value > entities_.size()) {
		throw InternalError("DeclarationBuilder: prepared merge publication has invalid EntityId");
	}
	const auto existing = entity_by_key_.find(key);
	if (existing == entity_by_key_.end() || existing->second != prepared.entity_id_) {
		throw InternalError("DeclarationBuilder: prepared merge publication key does not match live entity");
	}

	EntityRecord& live_entity = entities_[prepared.entity_id_.value - 1];
	const EntityId entity_id = live_entity.id;
	transaction.noteEntityMutation(entity_id, live_entity);

	DeclarationRecord decl_record{};
	decl_record.entity_id = entity_id;
	decl_record.previous_decl_id = live_entity.latest_decl_id;
	decl_record.lexical_scope_id = prepared.lexical_scope_id_;
	decl_record.name = prepared.name_;
	decl_record.signature_id = prepared.signature_id_;
	decl_record.return_type_id = prepared.return_type_id_;
	decl_record.kind = static_cast<uint8_t>(DeclKind::Function);
	decl_record.language_linkage = static_cast<uint8_t>(LanguageLinkage::CPlusPlus);
	decl_record.flags = prepared.flags_;
	decl_record.reserved = 0;

	const DeclId decl_id = allocateDeclaration(decl_record);
	live_entity.latest_decl_id = decl_id;
	if (hasFlag(prepared.flags_, DeclarationFlags::IsDefinition)) {
		live_entity.flags = static_cast<uint8_t>(live_entity.flags | DeclarationFlags::IsDefinition);
	}
	if (hasFlag(prepared.flags_, DeclarationFlags::IsInline)) {
		live_entity.flags = static_cast<uint8_t>(live_entity.flags | DeclarationFlags::IsInline);
	}

	return PublishResult{PublishStatus::MergedRedeclaration, decl_id, entity_id};
}

PublishResult DeclarationBuilder::publishFunction(
	const FunctionDeclRequest& request,
	const SymbolTable& symbol_table) {
	PreparedFunctionPublication prepared = prepareFunctionPublication(request, symbol_table);
	if (prepared.isRejected()) {
		return prepared.rejection();
	}
	PublicationTransaction transaction(*this);
	const PublishResult result = commitFunctionPublication(prepared, transaction);
	transaction.commit();
	return result;
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
	bool is_variadic,
	PublicationTransaction* transaction) {
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
	if (transaction != nullptr) {
		transaction->noteParameterListInsert(key);
	}
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
		func_decl.is_variadic(),
		nullptr);
	request.return_type_id =
		builder.internDeclaratorType(func_decl.decl_node().type_specifier_node());
	request.language_linkage = LanguageLinkage::CPlusPlus;
	request.is_definition = is_definition;
	request.is_inline = func_decl.is_inline();
	request.is_constexpr = func_decl.is_constexpr();
	return request;
}

FunctionDeclRequest buildFreeFunctionDeclRequest(
	DeclarationBuilder& builder,
	PublicationTransaction& transaction,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition) {
	FunctionDeclRequest request{};
	request.lexical_scope_id = lexical_scope_id;
	request.name = func_decl.decl_node().identifier_token().handle();
	request.signature_id = builder.internParameterListSignature(
		func_decl.parameter_nodes(),
		func_decl.is_variadic(),
		&transaction);
	request.return_type_id =
		builder.internDeclaratorType(func_decl.decl_node().type_specifier_node());
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
	, checkpoint_{
		  builder.declarations_.size(),
		  builder.entities_.size(),
		  builder.declarator_type_canon_.size()} {
	if (builder.active_publication_transactions_ != 0) {
		throw InternalError("PublicationTransaction: nested transactions are forbidden");
	}
	++builder.active_publication_transactions_;
	registered_ = true;
}

PublicationTransaction::~PublicationTransaction() noexcept {
	if (!committed_ && !rolled_back_) {
		rollback();
	}
	if (registered_) {
		--builder_.active_publication_transactions_;
	}
}

void PublicationTransaction::commit() noexcept {
	committed_ = true;
	if (registered_) {
		--builder_.active_publication_transactions_;
		registered_ = false;
	}
}

void PublicationTransaction::rollback() noexcept {
	if (rolled_back_ || committed_) {
		return;
	}

	while (builder_.declarations_.size() > checkpoint_.declaration_count) {
		builder_.declarations_.pop_back();
	}

	for (auto undo = entity_undos_.rbegin(); undo != entity_undos_.rend(); ++undo) {
		EntityRecord& live_entity = builder_.entities_[undo->id.value - 1];
		live_entity = undo->previous;
	}

	while (builder_.entities_.size() > checkpoint_.entity_count) {
		builder_.entities_.pop_back();
	}

	for (const DeclarationBuilder::EntityLookupKey& key : inserted_entity_lookups_) {
		builder_.entity_by_key_.erase(key);
	}

	while (builder_.declarator_type_canon_.size() > checkpoint_.declarator_type_count) {
		builder_.declarator_type_canon_.pop_back();
	}

	for (const DeclarationBuilder::ParameterListKey& key : inserted_parameter_lists_) {
		builder_.parameter_list_ids_.erase(key);
	}

	rolled_back_ = true;
}

void PublicationTransaction::noteEntityMutation(EntityId entity_id, const EntityRecord& previous) {
	entity_undos_.push_back(EntityUndo{entity_id, previous});
}

void PublicationTransaction::noteEntityLookupInsert(const DeclarationBuilder::EntityLookupKey& key) {
	inserted_entity_lookups_.push_back(key);
}

void PublicationTransaction::noteParameterListInsert(const DeclarationBuilder::ParameterListKey& key) {
	inserted_parameter_lists_.push_back(key);
}

PublishResult commitParserFreeFunctionPublication(
	DeclarationBuilder& builder,
	const FunctionDeclarationNode& func_decl,
	ScopeId lexical_scope_id,
	bool is_definition,
	const SymbolTable& symbol_table) {
	PublicationTransaction transaction(builder);
	const FunctionDeclRequest request = buildFreeFunctionDeclRequest(
		builder,
		transaction,
		func_decl,
		lexical_scope_id,
		is_definition);
	PreparedFunctionPublication prepared =
		builder.prepareFunctionPublication(request, symbol_table);
	if (prepared.isRejected()) {
		transaction.rollback();
		return prepared.rejection();
	}

	const PublishResult result = builder.commitFunctionPublication(prepared, transaction);
	transaction.commit();
	return result;
}
