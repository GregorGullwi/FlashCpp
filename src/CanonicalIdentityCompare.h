#pragma once

#include <cstdint>

#include "CanonicalShapeQueries.h"
#include "CanonicalTypeAdapter.h"

namespace FlashCpp {

// Canonical identity verdict for the flat-projection consumers that still hold
// pointer and array fields. A verdict is available only when both operands
// publish a canonical identity; otherwise the caller keeps its flat comparison
// for the shapes that have no identity of their own.
//
// Both imports live inside one rolled-back transaction. These predicates must
// not publish anything: a shared placeholder for an unpublished shape would
// merge types that have no identity.
enum class CanonicalIdentityVerdict : uint8_t {
	Unpublished,
	Identical,
	Distinct,
};

// Exact keeps top-level cv and references. IgnoringTopCvAndReference matches the
// value-type rule of return-type deduction, where top-level cv and references do
// not distinguish two return operands. Per-level pointer cv stays significant in
// both modes, so `int* const*` and `int**` never compare identical.
enum class CanonicalIdentityMode : uint8_t {
	Exact,
	IgnoringTopCvAndReference,
};

inline CanonicalIdentityVerdict canonicalTypeIdentityVerdict(CanonicalTypeTable& table, const TypeSpecifierNode& lhs,
	const TypeSpecifierNode& rhs, CanonicalIdentityMode mode) {
	CanonicalTypeTransaction transaction(table);
	const CanonicalTypeImport left = importCanonicalType(table, lhs);
	if (left.status != CanonicalTypeImportStatus::Supported) {
		transaction.rollback();
		return CanonicalIdentityVerdict::Unpublished;
	}
	const CanonicalTypeImport right = importCanonicalType(table, rhs);
	if (right.status != CanonicalTypeImportStatus::Supported) {
		transaction.rollback();
		return CanonicalIdentityVerdict::Unpublished;
	}
	const bool identical = mode == CanonicalIdentityMode::Exact
		? left.type == right.type
		: canonicalSameIgnoringTopCvRef(table, left.type, right.type);
	transaction.rollback();
	return identical ? CanonicalIdentityVerdict::Identical : CanonicalIdentityVerdict::Distinct;
}

} // namespace FlashCpp
