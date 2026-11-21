#pragma once

#include <array>
#include <cstdint>

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

inline void printCanonicalTypeTableEndOfCompileStats() {
	if (FrontendContext* active = FrontendContext::active()) {
		const CanonicalTypeTable& table = active->canonicalTypes();
		const CanonicalTypeArenaStats arena = table.arenaStats();
		FLASH_LOG_FORMAT(Types, Debug, "canonical-table end-of-compile nodes={} arena_used={} arena_reserved={}",
			table.size(), arena.used_bytes, arena.reserved_bytes);
	}
}