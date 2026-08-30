#include "DeclarationBuilder.h"

#include <stdexcept>

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

bool DeclarationBuilder::isValidRequest(const FunctionDeclRequest& request) const {
	if (!request.target_scope_id) {
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
		throw std::logic_error("DeclarationBuilder: DeclId space exhausted");
	}
	const DeclId id{static_cast<uint32_t>(declarations_.size() + 1)};
	record.id = id;
	declarations_.push_back(record);
	return id;
}

EntityId DeclarationBuilder::allocateEntity(EntityRecord record) {
	if (entities_.size() >= 0xffffffffu) {
		throw std::logic_error("DeclarationBuilder: EntityId space exhausted");
	}
	const EntityId id{static_cast<uint32_t>(entities_.size() + 1)};
	record.id = id;
	entities_.push_back(record);
	return id;
}

const DeclarationRecord& DeclarationBuilder::declaration(DeclId decl_id) const {
	if (!decl_id || decl_id.value > declarations_.size()) {
		throw std::logic_error("DeclarationBuilder: invalid DeclId");
	}
	return declarations_[decl_id.value - 1];
}

const EntityRecord& DeclarationBuilder::entity(EntityId entity_id) const {
	if (!entity_id || entity_id.value > entities_.size()) {
		throw std::logic_error("DeclarationBuilder: invalid EntityId");
	}
	return entities_[entity_id.value - 1];
}

PublishResult DeclarationBuilder::publishFunction(const FunctionDeclRequest& request) {
	if (!isValidRequest(request)) {
		return makeRejected(EntityId{});
	}

	const EntityLookupKey key{
		request.target_scope_id.value,
		request.name.handle,
		request.signature_id.value};

	const uint8_t incoming_flags = requestFlags(request);
	const auto existing = entity_by_key_.find(key);
	if (existing == entity_by_key_.end()) {
		EntityRecord entity_record{};
		entity_record.scope_id = request.target_scope_id;
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
		decl_record.scope_id = request.target_scope_id;
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

	EntityRecord& live_entity = entities_[existing->second.value - 1];
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

	DeclarationRecord decl_record{};
	decl_record.entity_id = entity_id;
	decl_record.previous_decl_id = live_entity.latest_decl_id;
	decl_record.scope_id = request.target_scope_id;
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
