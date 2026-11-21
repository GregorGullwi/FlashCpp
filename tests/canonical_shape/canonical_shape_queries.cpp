#include <cstdio>
#include <cstring>
#include <source_location>

#include "CanonicalShapeQueries.h"
#include "CanonicalTypeAdapter.h"

// Host regression for TypeId shape queries. It links the canonical table and the
// adapter only: no parser and no semantic-analysis member context.

void tryBindPublishedTypeEntity(TypeSpecifierNode&) {}

ChunkedStringAllocator gChunkedStringAllocator;
#ifdef _WIN32
TargetDataModel g_target_data_model = TargetDataModel::LLP64;
#else
TargetDataModel g_target_data_model = TargetDataModel::LP64;
#endif

namespace {

void requireShape(bool condition, const char* perturbation, std::source_location location) {
	if (!condition) {
		std::fprintf(stderr, "shape query failed at %s:%u\nperturbation: %s\n", location.file_name(), location.line(), perturbation);
		throw InternalError("canonical shape query regression failed");
	}
}

#define REQUIRE_SHAPE(condition, perturbation) requireShape((condition), (perturbation), std::source_location::current())

void requireExtents(const CanonicalTypeTable& table, TypeId type, std::span<const size_t> expected, const char* perturbation) {
	REQUIRE_SHAPE(canonicalArrayExtentCount(table, type) == expected.size(), perturbation);
	size_t actual[4] = {};
	REQUIRE_SHAPE(expected.size() <= 4, perturbation);
	canonicalCopyArrayExtents(table, type, std::span<size_t>(actual, expected.size()));
	for (size_t index = 0; index < expected.size(); ++index) {
		REQUIRE_SHAPE(actual[index] == expected[index], perturbation);
	}
}

void requirePointerCvs(const CanonicalTypeTable& table, TypeId type, std::span<const CVQualifier> expected, const char* perturbation) {
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, type) == expected.size(), perturbation);
	CVQualifier actual[4] = {};
	REQUIRE_SHAPE(expected.size() <= 4, perturbation);
	canonicalCopyPointerLevelCvs(table, type, std::span<CVQualifier>(actual, expected.size()));
	for (size_t index = 0; index < expected.size(); ++index) {
		REQUIRE_SHAPE(actual[index] == expected[index], perturbation);
	}
}

TypeSpecifierNode intSyntax() {
	return TypeSpecifierNode(TypeCategory::Int, TypeQualifier::None, 32, Token{}, CVQualifier::None);
}

// Perturbation: reporting the pointee as the pointer, or treating a reference to a pointer as a non-pointer.
void checkPlainPointer() {
	const char* perturbation = "treating the pointer node as its pointee, or failing to strip a reference";
	CanonicalTypeTable forward;
	const TypeId integer = forward.builtin(CanonicalBuiltinKind::Int);
	const TypeId pointer = forward.pointer(integer);
	const TypeId reference = forward.reference(pointer, ReferenceQualifier::LValueReference);
	REQUIRE_SHAPE(canonicalIsPointer(forward, pointer), perturbation);
	REQUIRE_SHAPE(canonicalIsPointer(forward, reference), perturbation);
	REQUIRE_SHAPE(canonicalPointee(forward, pointer) == integer, perturbation);
	REQUIRE_SHAPE(canonicalPointee(forward, reference) == integer, perturbation);
	REQUIRE_SHAPE(!canonicalArrayElement(forward, pointer).has_value(), perturbation);
	REQUIRE_SHAPE(canonicalArrayExtentCount(forward, pointer) == 0, perturbation);
	const CVQualifier none[] = {CVQualifier::None};
	requirePointerCvs(forward, pointer, none, perturbation);
	requirePointerCvs(forward, reference, none, perturbation);
	StringTable::getOrInternStringHandle("plain pointer spelling Z");
	REQUIRE_SHAPE(canonicalIsPointer(forward, pointer), perturbation);
	StringTable::getOrInternStringHandle("plain pointer spelling A");
	REQUIRE_SHAPE(canonicalPointee(forward, reference) == integer, perturbation);

	CanonicalTypeTable reverse;
	reverse.builtin(CanonicalBuiltinKind::Char);
	const TypeId reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_pointer = reverse.pointer(reverse_integer);
	REQUIRE_SHAPE(canonicalIsPointer(reverse, reverse_pointer), perturbation);
	REQUIRE_SHAPE(canonicalPointee(reverse, reverse_pointer) == reverse_integer, perturbation);
	requirePointerCvs(reverse, reverse.reference(reverse_pointer, ReferenceQualifier::LValueReference), none, perturbation);
}

// Perturbation: recording pointer cv inner-to-outer, or treating the pointee's const as another pointer level.
void checkMultiLevelCvPointer() {
	const char* perturbation = "reversing pointer-level cv or counting the pointee const as a pointer level";
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId inner = table.qualify(table.pointer(table.qualify(integer, CVQualifier::Const)), CVQualifier::Volatile);
	const TypeId outer = table.qualify(table.pointer(inner), CVQualifier::Const);
	const TypeId reference = table.reference(outer, ReferenceQualifier::RValueReference);
	REQUIRE_SHAPE(canonicalIsPointer(table, outer), perturbation);
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, outer) == 2, perturbation);
	const CVQualifier levels[] = {CVQualifier::Const, CVQualifier::Volatile};
	requirePointerCvs(table, outer, levels, perturbation);
	requirePointerCvs(table, reference, levels, perturbation);
	const TypeId pointee = canonicalPointee(table, outer).value();
	REQUIRE_SHAPE(canonicalIsPointer(table, pointee), perturbation);
	const CVQualifier inner_level[] = {CVQualifier::Volatile};
	requirePointerCvs(table, pointee, inner_level, perturbation);
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(table, outer, table.pointer(inner)), perturbation);
	const TypeId const_pointer = table.pointer(table.qualify(integer, CVQualifier::Const));
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(table, const_pointer, table.pointer(integer)), perturbation);
}

// Perturbation: reporting a known bound as unknown (extent 0) or as a non-array.
void checkArray() {
	const char* perturbation = "reporting int[4] as an unknown bound or as a non-array";
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId array = table.array(integer, 4);
	const size_t expected[] = {4};
	requireExtents(table, array, expected, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(table, array) == 4, perturbation);
	REQUIRE_SHAPE(canonicalArrayElement(table, array) == integer, perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(table, array), perturbation);
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, array) == 0, perturbation);
	const TypeId reference = table.reference(array, ReferenceQualifier::LValueReference);
	requireExtents(table, reference, expected, perturbation);
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(table, table.qualify(array, CVQualifier::Const), array), perturbation);
}

// Perturbation: swapping the outer and inner bounds.
void checkMultidimensionalArray() {
	const char* perturbation = "reversing multidimensional extents so [2][3] is reported as [3][2]";
	CanonicalTypeTable forward;
	const TypeId integer = forward.builtin(CanonicalBuiltinKind::Int);
	const TypeId inner = forward.array(integer, 3);
	const TypeId outer = forward.array(inner, 2);
	const size_t expected[] = {2, 3};
	requireExtents(forward, outer, expected, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(forward, outer) == 2, perturbation);
	REQUIRE_SHAPE(canonicalArrayElement(forward, outer) == inner, perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(forward, outer), perturbation);
	StringTable::getOrInternStringHandle("multi array spelling Z");
	requireExtents(forward, outer, expected, perturbation);

	CanonicalTypeTable reverse;
	reverse.builtin(CanonicalBuiltinKind::Long);
	StringTable::getOrInternStringHandle("multi array spelling A");
	const TypeId reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_outer = reverse.array(reverse.array(reverse_integer, 3), 2);
	requireExtents(reverse, reverse_outer, expected, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(reverse, reverse_outer) == 2, perturbation);
	const size_t swapped[] = {3, 2};
	REQUIRE_SHAPE(canonicalArrayExtentCount(reverse, reverse.array(reverse.array(reverse_integer, 2), 3)) == 2, perturbation);
	requireExtents(reverse, reverse.array(reverse.array(reverse_integer, 2), 3), swapped, perturbation);
}

// Perturbation: treating an unknown bound as "not an array" or as a bound of 1.
void checkUnknownBoundArray() {
	const char* perturbation = "collapsing an unknown bound into a non-array or into extent 1";
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId unknown = table.arrayOfUnknownBound(integer);
	const TypeId inner = table.array(integer, 3);
	const TypeId unknown_outer = table.arrayOfUnknownBound(inner);
	const size_t one_unknown[] = {0};
	const size_t two[] = {0, 3};
	requireExtents(table, unknown, one_unknown, perturbation);
	requireExtents(table, unknown_outer, two, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(table, unknown) == 0, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(table, unknown_outer) == 0, perturbation);
	REQUIRE_SHAPE(canonicalArrayElement(table, unknown) == integer, perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(table, unknown), perturbation);
	REQUIRE_SHAPE(!canonicalArrayElementCount(table, integer).has_value(), perturbation);
	TypeSpecifierNode syntax = intSyntax();
	syntax.set_ordered_declarator({DeclaratorComponent::unknownBoundArray()});
	const TypeId imported = requireCanonicalShape(table, syntax);
	requireExtents(table, imported, one_unknown, perturbation);
}

// Perturbation: classifying a pointer to an array as an array of pointers.
void checkPointerToArray() {
	const char* perturbation = "classifying int (*)[4] as an array of pointers";
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId array = table.array(integer, 4);
	const TypeId pointer = table.pointer(array);
	const TypeId array_of_pointer = table.array(table.pointer(integer), 4);
	REQUIRE_SHAPE(canonicalIsPointer(table, pointer), perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(table, array_of_pointer), perturbation);
	REQUIRE_SHAPE(canonicalArrayExtentCount(table, pointer) == 0, perturbation);
	REQUIRE_SHAPE(canonicalPointee(table, pointer) == array, perturbation);
	const size_t four[] = {4};
	requireExtents(table, canonicalPointee(table, pointer).value(), four, perturbation);
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(table, pointer, array_of_pointer), perturbation);
	TypeSpecifierNode syntax = intSyntax();
	syntax.set_ordered_declarator({DeclaratorComponent::pointer(CVQualifier::Const), DeclaratorComponent::array(4)});
	const TypeId imported = requireCanonicalShape(table, syntax);
	REQUIRE_SHAPE(canonicalIsPointer(table, imported), perturbation);
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(table, imported, table.qualify(pointer, CVQualifier::Const)), perturbation);
}

// Perturbation: classifying an array of pointers as a pointer to an array.
void checkInterleavedArrayOfPointer() {
	const char* perturbation = "classifying int *[3] as a pointer to an array";
	CanonicalTypeTable forward;
	const TypeId integer = forward.builtin(CanonicalBuiltinKind::Int);
	const TypeId pointer = forward.pointer(integer);
	const TypeId array = forward.array(pointer, 3);
	const TypeId pointer_to_array = forward.pointer(forward.array(integer, 3));
	const size_t three[] = {3};
	REQUIRE_SHAPE(!canonicalIsPointer(forward, array), perturbation);
	REQUIRE_SHAPE(canonicalIsPointer(forward, canonicalArrayElement(forward, array).value()), perturbation);
	requireExtents(forward, array, three, perturbation);
	REQUIRE_SHAPE(canonicalArrayElementCount(forward, array) == 3, perturbation);
	REQUIRE_SHAPE(canonicalPointee(forward, pointer_to_array) != pointer, perturbation);
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(forward, array, pointer_to_array), perturbation);
	StringTable::getOrInternStringHandle("interleaved spelling Z");
	requireExtents(forward, array, three, perturbation);

	CanonicalTypeTable reverse;
	StringTable::getOrInternStringHandle("interleaved spelling A");
	reverse.builtin(CanonicalBuiltinKind::Double);
	const TypeId reverse_integer = reverse.builtin(CanonicalBuiltinKind::Int);
	const TypeId reverse_array = reverse.array(reverse.pointer(reverse_integer), 3);
	requireExtents(reverse, reverse_array, three, perturbation);
	REQUIRE_SHAPE(canonicalIsPointer(reverse, canonicalArrayElement(reverse, reverse_array).value()), perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(reverse, reverse_array), perturbation);
}

// Perturbation: classifying a member pointer as an ordinary pointer or inventing an array extent.
void checkMemberPointer() {
	const char* perturbation = "treating a member pointer as Pointer or giving it pointer-level cv";
	CanonicalTypeTable table;
	const TypeId integer = table.builtin(CanonicalBuiltinKind::Int);
	const TypeId owner = table.record(EntityId{15});
	const TypeId other = table.record(EntityId{16});
	const TypeId member = table.memberObjectPointer(owner, integer);
	const TypeId other_member = table.memberObjectPointer(other, integer);
	const TypeId qualified = table.qualify(member, CVQualifier::Const);
	const TypeId ordinary = table.pointer(integer);
	REQUIRE_SHAPE(!canonicalIsPointer(table, member), perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(table, qualified), perturbation);
	REQUIRE_SHAPE(!canonicalPointee(table, member).has_value(), perturbation);
	REQUIRE_SHAPE(!canonicalArrayElement(table, member).has_value(), perturbation);
	REQUIRE_SHAPE(canonicalArrayExtentCount(table, member) == 0, perturbation);
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, member) == 0, perturbation);
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, qualified) == 0, perturbation);
	REQUIRE_SHAPE(table.memberPointerPointee(member) == integer, perturbation);
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(table, member, ordinary), perturbation);
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(table, qualified, member), perturbation);
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(table, member, other_member), perturbation);
	StringTable::getOrInternStringHandle("member pointer spelling A");
	REQUIRE_SHAPE(!canonicalIsPointer(table, member), perturbation);
	StringTable::getOrInternStringHandle("member pointer spelling Z");
	REQUIRE_SHAPE(table.memberPointerPointee(member) == integer, perturbation);
	REQUIRE_SHAPE(canonicalPointerLevelCount(table, qualified) == 0, perturbation);
}

// Perturbation: losing the template-parameter index, or depending on string-table insertion order.
void checkDependentTemplateParameter() {
	const char* perturbation = "dropping the template parameter index or keying the shape on string insertion order";
	CanonicalTypeTable forward;
	const TemplateDeclId environment{81};
	const TypeId parameter = forward.templateParameter(environment, 2);
	const TypeId pointer = forward.pointer(forward.qualify(parameter, CVQualifier::Const));
	const TypeId shape = forward.array(pointer, 4);
	const size_t four[] = {4};
	requireExtents(forward, shape, four, perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(forward, shape), perturbation);
	REQUIRE_SHAPE(!canonicalIsPointer(forward, parameter), perturbation);
	const TypeId element = canonicalArrayElement(forward, shape).value();
	REQUIRE_SHAPE(canonicalIsPointer(forward, element), perturbation);
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(forward, canonicalPointee(forward, element).value(), parameter), perturbation);
	REQUIRE_SHAPE(forward.templateParameterIndex(parameter) == 2, perturbation);
	REQUIRE_SHAPE(forward.templateParameterDecl(parameter) == environment, perturbation);
	StringTable::getOrInternStringHandle("dependent parameter spelling Z");
	requireExtents(forward, shape, four, perturbation);
	REQUIRE_SHAPE(forward.templateParameterIndex(parameter) == 2, perturbation);

	CanonicalTypeTable reverse;
	StringTable::getOrInternStringHandle("dependent parameter spelling A");
	reverse.array(reverse.builtin(CanonicalBuiltinKind::Char), 9);
	const TypeId reverse_parameter = reverse.templateParameter(environment, 2);
	const TypeId reverse_shape = reverse.array(reverse.pointer(reverse.qualify(reverse_parameter, CVQualifier::Const)), 4);
	requireExtents(reverse, reverse_shape, four, perturbation);
	const TypeId reverse_element = canonicalArrayElement(reverse, reverse_shape).value();
	const TypeId reverse_pointee = canonicalPointee(reverse, reverse_element).value();
	REQUIRE_SHAPE(canonicalSameIgnoringTopCvRef(reverse, reverse_pointee, reverse_parameter), perturbation);
	REQUIRE_SHAPE(reverse.templateParameterIndex(reverse_parameter) == 2, perturbation);
	REQUIRE_SHAPE(reverse.templateParameterDecl(reverse_parameter) == environment, perturbation);
	const TypeId other_parameter = reverse.templateParameter(environment, 0);
	REQUIRE_SHAPE(!canonicalSameIgnoringTopCvRef(reverse, reverse_parameter, other_parameter), perturbation);
}

// Perturbation: accepting a non-Supported import, or importing inside a speculative region.
void checkRequireCanonicalShape() {
	const char* perturbation = "accepting a non-Supported import or running requireCanonicalShape during speculative parsing";
	CanonicalTypeTable table;
	TypeSpecifierNode pointer_syntax = intSyntax();
	pointer_syntax.set_ordered_declarator({DeclaratorComponent::pointer(CVQualifier::None)});
	const TypeId imported = requireCanonicalShape(table, pointer_syntax);
	REQUIRE_SHAPE(canonicalIsPointer(table, imported), perturbation);
	REQUIRE_SHAPE(imported == table.pointer(table.builtin(CanonicalBuiltinKind::Int)), perturbation);

	TypeSpecifierNode automatic(TypeCategory::Auto, TypeQualifier::None, 0, Token{}, CVQualifier::None);
	bool named_status = false;
	try {
		(void)requireCanonicalShape(table, automatic);
	} catch (const InternalError& error) {
		named_status = std::strstr(error.what(), "Unresolved") != nullptr;
	}
	REQUIRE_SHAPE(named_status, perturbation);

	bool named_region = false;
	{
		SpeculativeParseRegion region;
		try {
			(void)requireCanonicalShape(table, pointer_syntax);
		} catch (const InternalError& error) {
			named_region = std::strstr(error.what(), "speculative parsing") != nullptr;
		}
	}
	REQUIRE_SHAPE(named_region, perturbation);
	REQUIRE_SHAPE(requireCanonicalShape(table, pointer_syntax) == imported, perturbation);

	const TypeId multi = table.array(table.array(table.builtin(CanonicalBuiltinKind::Int), 3), 2);
	size_t too_small[1] = {};
	bool named_span = false;
	try {
		canonicalCopyArrayExtents(table, multi, too_small);
	} catch (const InternalError& error) {
		named_span = std::strstr(error.what(), "too small") != nullptr;
	}
	REQUIRE_SHAPE(named_span, perturbation);
	CVQualifier cv_too_small[1] = {};
	named_span = false;
	const TypeId two_pointers = table.pointer(table.pointer(table.builtin(CanonicalBuiltinKind::Int)));
	try {
		canonicalCopyPointerLevelCvs(table, two_pointers, cv_too_small);
	} catch (const InternalError& error) {
		named_span = std::strstr(error.what(), "too small") != nullptr;
	}
	REQUIRE_SHAPE(named_span, perturbation);
}

} // namespace

int main() {
	try {
		checkPlainPointer();
		checkMultiLevelCvPointer();
		checkArray();
		checkMultidimensionalArray();
		checkUnknownBoundArray();
		checkPointerToArray();
		checkInterleavedArrayOfPointer();
		checkMemberPointer();
		checkDependentTemplateParameter();
		checkRequireCanonicalShape();
		return 0;
	} catch (const InternalError& error) {
		std::fprintf(stderr, "%s\n", error.what());
		return 1;
	}
}
