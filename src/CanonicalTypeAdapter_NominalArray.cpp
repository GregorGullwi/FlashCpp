#include "CanonicalTypeAdapter.h"

CanonicalTypeImport importCanonicalNominalArrayWithoutDirectEntity(CanonicalTypeTable& table,
	const TypeSpecifierNode& syntax, CanonicalTypeImportContext context) {
	const bool is_record = syntax.category() == TypeCategory::Struct;
	const bool has_ordinary_array = syntax.is_array() && !syntax.has_pointee_array_declarator();
	const bool has_pointee_array = syntax.has_pointee_array_declarator();
	const EntityId entity = publishedNominalEntity(syntax);
	if (entity) {
		return importPublishedNominalArray(table, syntax, context, entity, is_record, has_ordinary_array, has_pointee_array);
	}
	if (!is_record) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	CanonicalTypeTransaction transaction(table);
	const TypeInfo* type_info = tryGetTypeInfo(syntax.type_index());
	const StructTypeInfo* record = type_info != nullptr ? type_info->getStructInfo() : nullptr;
	const StructDeclarationNode* declaration = record != nullptr ? record->declaration_node : nullptr;
	const StructDeclarationNode* pattern = declaration != nullptr ? declaration->injected_class_pattern_declaration() : nullptr;
	// An instantiated class template publishes its pattern template, not an entity.
	// Array import reuses that specialization instead of inventing an entity.
	if (pattern == nullptr || !pattern->has_template_decl_id() || type_info == nullptr) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	const CanonicalTypeImport imported = importCanonicalClassTypeInfo(table, *type_info);
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		return {{}, CanonicalTypeImportStatus::UnmigratedNominal};
	}
	TypeId id = table.qualify(imported.type, syntax.cv_qualifier());
	const CanonicalTypeImport shaped = applyCanonicalPointerArrayReference(table, id, syntax, context, has_ordinary_array, has_pointee_array);
	if (shaped.status != CanonicalTypeImportStatus::Supported) {
		return shaped;
	}
	transaction.commit();
	return shaped;
}
