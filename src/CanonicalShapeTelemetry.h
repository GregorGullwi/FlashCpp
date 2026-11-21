#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "CanonicalShapeQueries.h"
#include "CanonicalTypeAdapter.h"
#include "CanonicalTypes.h"
#include "FrontendContext.h"
#include "Log.h"
#include "MigrationStats.h"
#include "MigrationTelemetryConfig.h"

#ifndef FLASHCPP_CANONICAL_SHAPE_TELEMETRY
#define FLASHCPP_CANONICAL_SHAPE_TELEMETRY 1
#endif

// Sampled import-gap telemetry for flat-projection consumer migration (P0).
// Each probe imports inside a rolled-back CanonicalTypeTransaction so table
// and arena growth stay unchanged.

enum class CanonicalShapeCapability : uint8_t {
	ArrayExtents,
	PointerPresenceCv,
	IdentityEquality,
	DerivedTypes,
	Count,
};

enum class CanonicalShapeSite : uint16_t {
	IrGenerator_Stmt_Decl_ArrayExtent,
	IrGenerator_MemberAccess_ArrayExtent,
	IrGenerator_Visitors_Decl_ArrayExtent,
	ConstExprEvaluator_Materialize_ArrayExtent,
	ConstExprEvalHelpers_ArrayExtent,
	ConstExprEvaluator_TryGetTypeSize_ArrayExtent,
	Parser_Statements_ArrayExtent,
	Parser_Templates_Params_ArrayExtent,
	SemanticAnalysis_ArrayExtent,
	TypeTraitEvaluator_PointerCv,
	Parser_Expr_QualLookup_PointerDepth,
	TemplateExpressionEquivalence_Identity,
	ConstExprEvaluator_Bindings_Identity,
	Parser_Expr_QualLookup_PointerLevelsIdentity,
	ConstExprEvaluator_MemberAccess_Derived,
	Parser_Expr_PrimaryUnary_Derived,
	IrGenerator_Expr_Conversions_Derived,
	SemanticAnalysis_AdoptStructuralRefresh,
	Count,
};

inline constexpr std::size_t kCanonicalShapeSiteCount = static_cast<std::size_t>(CanonicalShapeSite::Count);
inline constexpr std::size_t kCanonicalTypeImportStatusCount = 6;

inline const char* canonicalShapeSiteLabel(CanonicalShapeSite site) {
	switch (site) {
	case CanonicalShapeSite::IrGenerator_Stmt_Decl_ArrayExtent:
		return "IrGenerator_Stmt_Decl";
	case CanonicalShapeSite::IrGenerator_MemberAccess_ArrayExtent:
		return "IrGenerator_MemberAccess";
	case CanonicalShapeSite::IrGenerator_Visitors_Decl_ArrayExtent:
		return "IrGenerator_Visitors_Decl";
	case CanonicalShapeSite::ConstExprEvaluator_Materialize_ArrayExtent:
		return "ConstExprEvaluator_Materialize";
	case CanonicalShapeSite::ConstExprEvalHelpers_ArrayExtent:
		return "ConstExprEvalHelpers";
	case CanonicalShapeSite::ConstExprEvaluator_TryGetTypeSize_ArrayExtent:
		return "ConstExprEvaluator_tryGetConstexprTypeSizeBytes";
	case CanonicalShapeSite::Parser_Statements_ArrayExtent:
		return "Parser_Statements";
	case CanonicalShapeSite::Parser_Templates_Params_ArrayExtent:
		return "Parser_Templates_Params";
	case CanonicalShapeSite::SemanticAnalysis_ArrayExtent:
		return "SemanticAnalysis";
	case CanonicalShapeSite::TypeTraitEvaluator_PointerCv:
		return "TypeTraitEvaluator";
	case CanonicalShapeSite::Parser_Expr_QualLookup_PointerDepth:
		return "Parser_Expr_QualLookup_pointer_depth";
	case CanonicalShapeSite::TemplateExpressionEquivalence_Identity:
		return "TemplateExpressionEquivalence";
	case CanonicalShapeSite::ConstExprEvaluator_Bindings_Identity:
		return "ConstExprEvaluator_Bindings";
	case CanonicalShapeSite::Parser_Expr_QualLookup_PointerLevelsIdentity:
		return "Parser_Expr_QualLookup_pointer_levels";
	case CanonicalShapeSite::ConstExprEvaluator_MemberAccess_Derived:
		return "ConstExprEvaluator_MemberAccess";
	case CanonicalShapeSite::Parser_Expr_PrimaryUnary_Derived:
		return "Parser_Expr_PrimaryUnary";
	case CanonicalShapeSite::IrGenerator_Expr_Conversions_Derived:
		return "IrGenerator_Expr_Conversions";
	case CanonicalShapeSite::SemanticAnalysis_AdoptStructuralRefresh:
		return "SemanticAnalysis_adoptPublishedStructuralType";
	case CanonicalShapeSite::Count:
		break;
	}
	return "unknown";
}

inline const char* canonicalTypeImportStatusLabel(CanonicalTypeImportStatus status) {
	switch (status) {
	case CanonicalTypeImportStatus::Supported:
		return "Supported";
	case CanonicalTypeImportStatus::UnmigratedArray:
		return "UnmigratedArray";
	case CanonicalTypeImportStatus::UnmigratedCallable:
		return "UnmigratedCallable";
	case CanonicalTypeImportStatus::UnmigratedNominal:
		return "UnmigratedNominal";
	case CanonicalTypeImportStatus::Unresolved:
		return "Unresolved";
	case CanonicalTypeImportStatus::Invalid:
		return "Invalid";
	}
	return "Unknown";
}

inline std::array<std::array<uint64_t, kCanonicalTypeImportStatusCount>, kCanonicalShapeSiteCount> gCanonicalShapeImportHistogram{};

inline CanonicalTypeTable* gCanonicalShapeProbeTable = nullptr;

struct CanonicalShapeProbeTableGuard {
	explicit CanonicalShapeProbeTableGuard(CanonicalTypeTable& table) { gCanonicalShapeProbeTable = &table; }
	~CanonicalShapeProbeTableGuard() { gCanonicalShapeProbeTable = nullptr; }
	CanonicalShapeProbeTableGuard(const CanonicalShapeProbeTableGuard&) = delete;
	CanonicalShapeProbeTableGuard& operator=(const CanonicalShapeProbeTableGuard&) = delete;
};

inline std::size_t canonicalTypeImportStatusIndex(CanonicalTypeImportStatus status) {
	return static_cast<std::size_t>(status);
}

inline const char* canonicalShapeCategoryLabel(TypeCategory category) {
	switch (category) {
	case TypeCategory::Invalid: return "Invalid";
	case TypeCategory::Void: return "Void";
	case TypeCategory::Bool: return "Bool";
	case TypeCategory::Char: return "Char";
	case TypeCategory::UnsignedChar: return "UnsignedChar";
	case TypeCategory::WChar: return "WChar";
	case TypeCategory::Char8: return "Char8";
	case TypeCategory::Char16: return "Char16";
	case TypeCategory::Char32: return "Char32";
	case TypeCategory::Short: return "Short";
	case TypeCategory::UnsignedShort: return "UnsignedShort";
	case TypeCategory::Int: return "Int";
	case TypeCategory::UnsignedInt: return "UnsignedInt";
	case TypeCategory::Long: return "Long";
	case TypeCategory::UnsignedLong: return "UnsignedLong";
	case TypeCategory::LongLong: return "LongLong";
	case TypeCategory::UnsignedLongLong: return "UnsignedLongLong";
	case TypeCategory::Float: return "Float";
	case TypeCategory::Double: return "Double";
	case TypeCategory::LongDouble: return "LongDouble";
	case TypeCategory::FunctionPointer: return "FunctionPointer";
	case TypeCategory::MemberFunctionPointer: return "MemberFunctionPointer";
	case TypeCategory::MemberObjectPointer: return "MemberObjectPointer";
	case TypeCategory::UserDefined: return "UserDefined";
	case TypeCategory::TypeAlias: return "TypeAlias";
	case TypeCategory::Auto: return "Auto";
	case TypeCategory::DeclTypeAuto: return "DeclTypeAuto";
	case TypeCategory::Function: return "Function";
	case TypeCategory::Struct: return "Struct";
	case TypeCategory::Enum: return "Enum";
	case TypeCategory::Nullptr: return "Nullptr";
	case TypeCategory::Template: return "Template";
	}
	return "Unknown";
}

inline void noteCanonicalShapeImport(CanonicalTypeTable& table, const TypeSpecifierNode& syntax, CanonicalShapeSite site) {
#if !FLASHCPP_CANONICAL_SHAPE_TELEMETRY
	(void)table;
	(void)syntax;
	(void)site;
	return;
#else
	const size_t nodes_before = table.size();
	const CanonicalTypeArenaStats arena_before = table.arenaStats();
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport imported = importCanonicalType(table, syntax);
	transaction.rollback();
	if (table.size() != nodes_before) {
		throw InternalError("canonical shape probe published nodes after rollback");
	}
	const CanonicalTypeArenaStats arena_after = table.arenaStats();
	if (arena_after.used_bytes != arena_before.used_bytes) {
		throw InternalError("canonical shape probe changed canonical arena used bytes after rollback");
	}
	const std::size_t site_index = static_cast<std::size_t>(site);
	const std::size_t status_index = canonicalTypeImportStatusIndex(imported.status);
	++gCanonicalShapeImportHistogram[site_index][status_index];
	if (imported.status != CanonicalTypeImportStatus::Supported) {
		recordCanonicalShapeImportGap();
		const std::span<const size_t> dimensions = syntax.array_dimensions();
		const size_t dimension_count = dimensions.size();
		const size_t first_dimension = dimension_count > 0 ? dimensions[0] : 0;
		const size_t second_dimension = dimension_count > 1 ? dimensions[1] : 0;
		const EntityId nominal_entity = publishedNominalEntity(syntax);
		const int nominal_layout = nominal_entity && syntax.category() == TypeCategory::Struct && table.hasRecordLayout(nominal_entity) ? 1 :
			nominal_entity && syntax.category() == TypeCategory::Enum && table.hasEnumLayout(nominal_entity) ? 1 : 0;
		FLASH_LOG_FORMAT(Types, Debug,
			"canonical-shape-import-shape site={} status={} category={} array={} dims={} d0={} d1={} ptr={} "
			"ordered={} pointee_array={} ref={} entity={} layout={}",
			canonicalShapeSiteLabel(site), canonicalTypeImportStatusLabel(imported.status), canonicalShapeCategoryLabel(syntax.category()),
			syntax.is_array() ? 1 : 0, dimension_count, first_dimension, second_dimension, syntax.pointer_depth(),
			syntax.has_ordered_declarator() ? 1 : 0, syntax.has_pointee_array_declarator() ? 1 : 0,
			static_cast<unsigned>(syntax.reference_qualifier()), nominal_entity ? 1 : 0, nominal_layout);
	}
#endif
}

inline void printCanonicalShapeImportHistogram() {
#if FLASHCPP_CANONICAL_SHAPE_TELEMETRY
	for (std::size_t site_index = 0; site_index < kCanonicalShapeSiteCount; ++site_index) {
		for (std::size_t status_index = 0; status_index < kCanonicalTypeImportStatusCount; ++status_index) {
			const uint64_t count = gCanonicalShapeImportHistogram[site_index][status_index];
			if (count == 0) {
				continue;
			}
			const auto site = static_cast<CanonicalShapeSite>(site_index);
			const auto status = static_cast<CanonicalTypeImportStatus>(status_index);
			FLASH_LOG_FORMAT(Types, Debug, "canonical-shape-import-histogram site={} status={} count={}",
				canonicalShapeSiteLabel(site), canonicalTypeImportStatusLabel(status), count);
		}
	}
#endif
}

// A canonical pointee or array element, written back as the flat pointer depth and array extents the consumer peels.
// KeepArrayObject matches remove_outermost_array_dimension, which leaves is_array set after the last bound is gone.
enum class DerivedArrayRankZero : uint8_t {
	ClearArray,
	KeepArrayObject,
};

inline void projectCanonicalDerivedShape(TypeSpecifierNode& syntax, const CanonicalTypeTable& table, TypeId derived, DerivedArrayRankZero rank_zero) {
	syntax.limit_pointer_depth(0);
	const size_t depth = canonicalPointerLevelCount(table, derived);
	if (depth > 8) {
		throw InternalError("canonical derived pointer depth exceeds the flat projection buffer");
	}
	if (depth > 0) {
		CVQualifier cvs[8] = {};
		canonicalCopyPointerLevelCvs(table, derived, std::span<CVQualifier>(cvs, depth));
		for (size_t index = 0; index < depth; ++index) {
			syntax.add_pointer_level(cvs[index]);
		}
	}
	const size_t rank = canonicalArrayExtentCount(table, derived);
	if (rank > 8) {
		throw InternalError("canonical derived array rank exceeds the flat projection buffer");
	}
	if (rank == 0) {
		if (rank_zero == DerivedArrayRankZero::KeepArrayObject && syntax.is_array() && !syntax.has_pointee_array_declarator()) {
			syntax.set_array(true, std::nullopt);
			return;
		}
		syntax.set_array(false, std::nullopt);
		return;
	}
	size_t extents[8] = {};
	canonicalCopyArrayExtents(table, derived, std::span<size_t>(extents, rank));
	syntax.set_array_dimensions(std::span<const size_t>(extents, rank));
}

// storage must outlive the returned span. Unknown bounds are extent 0.
inline std::span<const size_t> copyRequiredArrayExtents(CanonicalTypeTable& table, const TypeSpecifierNode& syntax, std::vector<size_t>& storage) {
	const TypeId shape = requireCanonicalShape(table, syntax);
	const size_t rank = canonicalArrayExtentCount(table, shape);
	storage.resize(rank);
	canonicalCopyArrayExtents(table, shape, storage);
	return storage;
}

// A descriptor that reached an array-extent consumer without structural_type_id
// is a publication defect. Unknown bounds are extent 0.
inline std::span<const size_t> copyPublishedArrayExtents(const CanonicalTypeTable& table, TypeId structural_type_id, std::vector<size_t>& storage) {
	if (!structural_type_id) {
		throw InternalError("array extent descriptor has no published structural identity");
	}
	const size_t rank = canonicalArrayExtentCount(table, structural_type_id);
	storage.resize(rank);
	canonicalCopyArrayExtents(table, structural_type_id, storage);
	return storage;
}

inline void printCanonicalTypeTableEndOfCompileStats() {
	if (FrontendContext* active = FrontendContext::active()) {
		const CanonicalTypeTable& table = active->canonicalTypes();
		const CanonicalTypeArenaStats arena = table.arenaStats();
		FLASH_LOG_FORMAT(Types, Debug, "canonical-table end-of-compile nodes={} arena_used={} arena_reserved={}",
			table.size(), arena.used_bytes, arena.reserved_bytes);
	}
}