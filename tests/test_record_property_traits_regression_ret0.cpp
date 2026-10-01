// Regression coverage for record-property trait rules shared by constant
// evaluation and runtime code generation.

struct EmptyRecord {
};

struct AggregateWithUserDeclaredDestructor {
	int value;
	~AggregateWithUserDeclaredDestructor() {
	}
};

union ScalarUnion {
	int integer;
	double real;
};

struct BaseWithData {
	int base_value;
};

struct DerivedFromData : BaseWithData {
};

struct DerivedWithAdditionalData : BaseWithData {
	int derived_value;
};

struct MiddleWithAdditionalData : BaseWithData {
	short middle_value;
};

struct LeafWithAdditionalData : MiddleWithAdditionalData {
	char leaf_value;
};

struct VirtualFunctionOnly {
	virtual void f() {
	}
};

struct VirtualDestructorBase {
	virtual ~VirtualDestructorBase() = default;
};

struct VirtualDestructorThroughVirtualBase : virtual VirtualDestructorBase {
};

struct DefaultedOnFirstDeclarationDestructor {
	~DefaultedOnFirstDeclarationDestructor() = default;
};

struct DefaultedAfterDeclarationDestructor {
	~DefaultedAfterDeclarationDestructor();
};

DefaultedAfterDeclarationDestructor::~DefaultedAfterDeclarationDestructor() = default;

struct NontrivialDestructorMember {
	~NontrivialDestructorMember() {
	}
};

struct NontrivialDestructor {
	~NontrivialDestructor() {
	}
};

struct ContainsNontrivialDestructorMember {
	NontrivialDestructorMember member;
};

struct ContainsNontrivialDestructorArray {
	NontrivialDestructorMember members[2];
};

static_assert(__is_aggregate(EmptyRecord));
static_assert(__is_aggregate(AggregateWithUserDeclaredDestructor));
static_assert(__is_aggregate(int[3]));
static_assert(__is_aggregate(EmptyRecord[2]));
static_assert(__is_pod(ScalarUnion));
static_assert(__is_standard_layout(ScalarUnion));
static_assert(__is_standard_layout(DerivedFromData));
static_assert(!__is_standard_layout(DerivedWithAdditionalData));
static_assert(!__is_standard_layout(LeafWithAdditionalData));
static_assert(!__is_pod(DerivedWithAdditionalData));
static_assert(!__is_pod(NontrivialDestructor));
static_assert(!__is_pod(ContainsNontrivialDestructorMember));
static_assert(__is_trivially_destructible(VirtualFunctionOnly));
static_assert(!__is_trivially_destructible(NontrivialDestructor));
static_assert(!__is_trivially_destructible(ContainsNontrivialDestructorMember));
static_assert(!__is_trivially_destructible(ContainsNontrivialDestructorArray));
static_assert(!__has_trivial_destructor(ContainsNontrivialDestructorMember));
static_assert(__is_trivially_destructible(DefaultedOnFirstDeclarationDestructor));
static_assert(!__is_trivially_destructible(DefaultedAfterDeclarationDestructor));
static_assert(__is_trivially_copyable(DefaultedOnFirstDeclarationDestructor));
static_assert(!__is_trivially_copyable(DefaultedAfterDeclarationDestructor));
static_assert(__is_trivial(DefaultedOnFirstDeclarationDestructor));
static_assert(!__is_trivial(DefaultedAfterDeclarationDestructor));
static_assert(__has_virtual_destructor(VirtualDestructorBase));
static_assert(__has_virtual_destructor(VirtualDestructorThroughVirtualBase));
static_assert(!__has_virtual_destructor(VirtualFunctionOnly));

int main() {
	int mismatches = 0;
	int runtime_aggregate = __is_aggregate(EmptyRecord);
	int runtime_aggregate_with_destructor = __is_aggregate(AggregateWithUserDeclaredDestructor);
	int runtime_aggregate_builtin_array = __is_aggregate(int[3]);
	int runtime_aggregate_record_array = __is_aggregate(EmptyRecord[2]);
	int runtime_pod_union = __is_pod(ScalarUnion);
	int runtime_union_standard_layout = __is_standard_layout(ScalarUnion);
	int runtime_base_only_standard_layout = __is_standard_layout(DerivedFromData);
	int runtime_standard_layout = __is_standard_layout(DerivedWithAdditionalData);
	int runtime_nested_standard_layout = __is_standard_layout(LeafWithAdditionalData);
	int runtime_derived_pod = __is_pod(DerivedWithAdditionalData);
	int runtime_user_dtor_pod = __is_pod(NontrivialDestructor);
	int runtime_member_dtor_pod = __is_pod(ContainsNontrivialDestructorMember);
	int runtime_virtual_function_dtor = __is_trivially_destructible(VirtualFunctionOnly);
	int runtime_user_dtor = __is_trivially_destructible(NontrivialDestructor);
	int runtime_member_dtor = __is_trivially_destructible(ContainsNontrivialDestructorMember);
	int runtime_array_member_dtor = __is_trivially_destructible(ContainsNontrivialDestructorArray);
	int runtime_member_dtor_alias = __has_trivial_destructor(ContainsNontrivialDestructorMember);
	int runtime_first_decl_defaulted_dtor = __is_trivially_destructible(DefaultedOnFirstDeclarationDestructor);
	int runtime_out_of_line_defaulted_dtor = __is_trivially_destructible(DefaultedAfterDeclarationDestructor);
	int runtime_first_decl_defaulted_copy = __is_trivially_copyable(DefaultedOnFirstDeclarationDestructor);
	int runtime_out_of_line_defaulted_copy = __is_trivially_copyable(DefaultedAfterDeclarationDestructor);
	int runtime_direct_virtual_dtor = __has_virtual_destructor(VirtualDestructorBase);
	int runtime_virtual_base_dtor = __has_virtual_destructor(VirtualDestructorThroughVirtualBase);
	int runtime_non_dtor_virtual = __has_virtual_destructor(VirtualFunctionOnly);
	mismatches |= runtime_aggregate ? 0 : 1;
	mismatches |= runtime_aggregate_with_destructor ? 0 : 2;
	mismatches |= runtime_aggregate_builtin_array ? 0 : 4194304;
	mismatches |= runtime_aggregate_record_array ? 0 : 8388608;
	mismatches |= runtime_pod_union ? 0 : 4;
	mismatches |= runtime_union_standard_layout ? 0 : 512;
	mismatches |= runtime_base_only_standard_layout ? 0 : 1024;
	mismatches |= runtime_standard_layout ? 8 : 0;
	mismatches |= runtime_nested_standard_layout ? 16384 : 0;
	mismatches |= runtime_derived_pod ? 32768 : 0;
	mismatches |= runtime_user_dtor_pod ? 262144 : 0;
	mismatches |= runtime_member_dtor_pod ? 65536 : 0;
	mismatches |= runtime_virtual_function_dtor ? 0 : 16;
	mismatches |= runtime_user_dtor ? 524288 : 0;
	mismatches |= runtime_member_dtor ? 32 : 0;
	mismatches |= runtime_array_member_dtor ? 131072 : 0;
	mismatches |= runtime_member_dtor_alias ? 64 : 0;
	mismatches |= runtime_first_decl_defaulted_dtor ? 0 : 2048;
	mismatches |= runtime_out_of_line_defaulted_dtor ? 4096 : 0;
	mismatches |= runtime_first_decl_defaulted_copy ? 0 : 1048576;
	mismatches |= runtime_out_of_line_defaulted_copy ? 2097152 : 0;
	mismatches |= runtime_direct_virtual_dtor ? 0 : 128;
	mismatches |= runtime_virtual_base_dtor ? 0 : 256;
	mismatches |= runtime_non_dtor_virtual ? 8192 : 0;
	return mismatches;
}
