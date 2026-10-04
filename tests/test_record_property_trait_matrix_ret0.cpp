// Per-cell clang differential for the 14 record-property traits across 21
// ordinary record shapes. Static assertions keep every folded cell independent;
// the runtime assignments exercise the same answers through code generation.

struct Shape00Empty {};
struct Shape01FinalEmpty final {};
struct Shape02PlainData { int value; };
struct Shape03AggregateDtor { int value; ~Shape03AggregateDtor() {} };
struct Shape04UserConstructor { Shape04UserConstructor() {} };
struct Shape05DeletedCopy { Shape05DeletedCopy(const Shape05DeletedCopy&) = delete; };
struct Shape06DefaultedDtor { ~Shape06DefaultedDtor() = default; };
struct Shape07OutOfLineDtor { ~Shape07OutOfLineDtor(); };
Shape07OutOfLineDtor::~Shape07OutOfLineDtor() = default;
struct Shape08NontrivialDtor { ~Shape08NontrivialDtor() {} };
struct Shape09DtorMember { Shape08NontrivialDtor member; };
struct Shape10DtorArrayMember { Shape08NontrivialDtor members[2]; };
struct Shape11VirtualFunction { virtual void f() {} };
struct Shape12VirtualDestructor { virtual ~Shape12VirtualDestructor() = default; };
struct Shape13VirtualDestructorBase { virtual ~Shape13VirtualDestructorBase() = default; };
struct Shape13VirtualDtorViaVirtualBase : virtual Shape13VirtualDestructorBase {};
struct Shape14Abstract { virtual void f() = 0; };
union Shape15ScalarUnion { int integer; double real; };
union Shape16NontrivialUnion { Shape08NontrivialDtor member; ~Shape16NontrivialUnion() {} };
struct Shape17DataBase { int base; };
struct Shape17DerivedFromDataBase : Shape17DataBase {};
struct Shape18DerivedOwnData : Shape17DataBase { short own; };
struct Shape19PrivateData { private: ~Shape19PrivateData() {} public: int value; };
struct Shape20VirtualBaseNoVirtualFunctions : virtual Shape00Empty {
	Shape20VirtualBaseNoVirtualFunctions() = default;
};

#define RECORD_PROPERTY_MATRIX(X) \
	X(__is_aggregate, Shape00Empty, 1) \
	X(__is_pod, Shape00Empty, 1) \
	X(__is_standard_layout, Shape00Empty, 1) \
	X(__is_empty, Shape00Empty, 1) \
	X(__is_polymorphic, Shape00Empty, 0) \
	X(__is_final, Shape00Empty, 0) \
	X(__is_abstract, Shape00Empty, 0) \
	X(__is_destructible, Shape00Empty, 1) \
	X(__is_trivially_destructible, Shape00Empty, 1) \
	X(__is_nothrow_destructible, Shape00Empty, 1) \
	X(__is_trivially_copyable, Shape00Empty, 1) \
	X(__is_trivial, Shape00Empty, 1) \
	X(__has_trivial_destructor, Shape00Empty, 1) \
	X(__has_virtual_destructor, Shape00Empty, 0) \
	X(__is_aggregate, Shape01FinalEmpty, 1) \
	X(__is_pod, Shape01FinalEmpty, 1) \
	X(__is_standard_layout, Shape01FinalEmpty, 1) \
	X(__is_empty, Shape01FinalEmpty, 1) \
	X(__is_polymorphic, Shape01FinalEmpty, 0) \
	X(__is_final, Shape01FinalEmpty, 1) \
	X(__is_abstract, Shape01FinalEmpty, 0) \
	X(__is_destructible, Shape01FinalEmpty, 1) \
	X(__is_trivially_destructible, Shape01FinalEmpty, 1) \
	X(__is_nothrow_destructible, Shape01FinalEmpty, 1) \
	X(__is_trivially_copyable, Shape01FinalEmpty, 1) \
	X(__is_trivial, Shape01FinalEmpty, 1) \
	X(__has_trivial_destructor, Shape01FinalEmpty, 1) \
	X(__has_virtual_destructor, Shape01FinalEmpty, 0) \
	X(__is_aggregate, Shape02PlainData, 1) \
	X(__is_pod, Shape02PlainData, 1) \
	X(__is_standard_layout, Shape02PlainData, 1) \
	X(__is_empty, Shape02PlainData, 0) \
	X(__is_polymorphic, Shape02PlainData, 0) \
	X(__is_final, Shape02PlainData, 0) \
	X(__is_abstract, Shape02PlainData, 0) \
	X(__is_destructible, Shape02PlainData, 1) \
	X(__is_trivially_destructible, Shape02PlainData, 1) \
	X(__is_nothrow_destructible, Shape02PlainData, 1) \
	X(__is_trivially_copyable, Shape02PlainData, 1) \
	X(__is_trivial, Shape02PlainData, 1) \
	X(__has_trivial_destructor, Shape02PlainData, 1) \
	X(__has_virtual_destructor, Shape02PlainData, 0) \
	X(__is_aggregate, Shape03AggregateDtor, 1) \
	X(__is_pod, Shape03AggregateDtor, 0) \
	X(__is_standard_layout, Shape03AggregateDtor, 1) \
	X(__is_empty, Shape03AggregateDtor, 0) \
	X(__is_polymorphic, Shape03AggregateDtor, 0) \
	X(__is_final, Shape03AggregateDtor, 0) \
	X(__is_abstract, Shape03AggregateDtor, 0) \
	X(__is_destructible, Shape03AggregateDtor, 1) \
	X(__is_trivially_destructible, Shape03AggregateDtor, 0) \
	X(__is_nothrow_destructible, Shape03AggregateDtor, 1) \
	X(__is_trivially_copyable, Shape03AggregateDtor, 0) \
	X(__is_trivial, Shape03AggregateDtor, 0) \
	X(__has_trivial_destructor, Shape03AggregateDtor, 0) \
	X(__has_virtual_destructor, Shape03AggregateDtor, 0) \
	X(__is_aggregate, Shape04UserConstructor, 0) \
	X(__is_pod, Shape04UserConstructor, 0) \
	X(__is_standard_layout, Shape04UserConstructor, 1) \
	X(__is_empty, Shape04UserConstructor, 1) \
	X(__is_polymorphic, Shape04UserConstructor, 0) \
	X(__is_final, Shape04UserConstructor, 0) \
	X(__is_abstract, Shape04UserConstructor, 0) \
	X(__is_destructible, Shape04UserConstructor, 1) \
	X(__is_trivially_destructible, Shape04UserConstructor, 1) \
	X(__is_nothrow_destructible, Shape04UserConstructor, 1) \
	X(__is_trivially_copyable, Shape04UserConstructor, 1) \
	X(__is_trivial, Shape04UserConstructor, 0) \
	X(__has_trivial_destructor, Shape04UserConstructor, 1) \
	X(__has_virtual_destructor, Shape04UserConstructor, 0) \
	X(__is_aggregate, Shape05DeletedCopy, 0) \
	X(__is_pod, Shape05DeletedCopy, 0) \
	X(__is_standard_layout, Shape05DeletedCopy, 1) \
	X(__is_empty, Shape05DeletedCopy, 1) \
	X(__is_polymorphic, Shape05DeletedCopy, 0) \
	X(__is_final, Shape05DeletedCopy, 0) \
	X(__is_abstract, Shape05DeletedCopy, 0) \
	X(__is_destructible, Shape05DeletedCopy, 1) \
	X(__is_trivially_destructible, Shape05DeletedCopy, 1) \
	X(__is_nothrow_destructible, Shape05DeletedCopy, 1) \
	X(__is_trivially_copyable, Shape05DeletedCopy, 1) \
	X(__is_trivial, Shape05DeletedCopy, 0) \
	X(__has_trivial_destructor, Shape05DeletedCopy, 1) \
	X(__has_virtual_destructor, Shape05DeletedCopy, 0) \
	X(__is_aggregate, Shape06DefaultedDtor, 1) \
	X(__is_pod, Shape06DefaultedDtor, 1) \
	X(__is_standard_layout, Shape06DefaultedDtor, 1) \
	X(__is_empty, Shape06DefaultedDtor, 1) \
	X(__is_polymorphic, Shape06DefaultedDtor, 0) \
	X(__is_final, Shape06DefaultedDtor, 0) \
	X(__is_abstract, Shape06DefaultedDtor, 0) \
	X(__is_destructible, Shape06DefaultedDtor, 1) \
	X(__is_trivially_destructible, Shape06DefaultedDtor, 1) \
	X(__is_nothrow_destructible, Shape06DefaultedDtor, 1) \
	X(__is_trivially_copyable, Shape06DefaultedDtor, 1) \
	X(__is_trivial, Shape06DefaultedDtor, 1) \
	X(__has_trivial_destructor, Shape06DefaultedDtor, 1) \
	X(__has_virtual_destructor, Shape06DefaultedDtor, 0) \
	X(__is_aggregate, Shape07OutOfLineDtor, 1) \
	X(__is_pod, Shape07OutOfLineDtor, 0) \
	X(__is_standard_layout, Shape07OutOfLineDtor, 1) \
	X(__is_empty, Shape07OutOfLineDtor, 1) \
	X(__is_polymorphic, Shape07OutOfLineDtor, 0) \
	X(__is_final, Shape07OutOfLineDtor, 0) \
	X(__is_abstract, Shape07OutOfLineDtor, 0) \
	X(__is_destructible, Shape07OutOfLineDtor, 1) \
	X(__is_trivially_destructible, Shape07OutOfLineDtor, 0) \
	X(__is_nothrow_destructible, Shape07OutOfLineDtor, 1) \
	X(__is_trivially_copyable, Shape07OutOfLineDtor, 0) \
	X(__is_trivial, Shape07OutOfLineDtor, 0) \
	X(__has_trivial_destructor, Shape07OutOfLineDtor, 0) \
	X(__has_virtual_destructor, Shape07OutOfLineDtor, 0) \
	X(__is_aggregate, Shape08NontrivialDtor, 1) \
	X(__is_pod, Shape08NontrivialDtor, 0) \
	X(__is_standard_layout, Shape08NontrivialDtor, 1) \
	X(__is_empty, Shape08NontrivialDtor, 1) \
	X(__is_polymorphic, Shape08NontrivialDtor, 0) \
	X(__is_final, Shape08NontrivialDtor, 0) \
	X(__is_abstract, Shape08NontrivialDtor, 0) \
	X(__is_destructible, Shape08NontrivialDtor, 1) \
	X(__is_trivially_destructible, Shape08NontrivialDtor, 0) \
	X(__is_nothrow_destructible, Shape08NontrivialDtor, 1) \
	X(__is_trivially_copyable, Shape08NontrivialDtor, 0) \
	X(__is_trivial, Shape08NontrivialDtor, 0) \
	X(__has_trivial_destructor, Shape08NontrivialDtor, 0) \
	X(__has_virtual_destructor, Shape08NontrivialDtor, 0) \
	X(__is_aggregate, Shape09DtorMember, 1) \
	X(__is_pod, Shape09DtorMember, 0) \
	X(__is_standard_layout, Shape09DtorMember, 1) \
	X(__is_empty, Shape09DtorMember, 0) \
	X(__is_polymorphic, Shape09DtorMember, 0) \
	X(__is_final, Shape09DtorMember, 0) \
	X(__is_abstract, Shape09DtorMember, 0) \
	X(__is_destructible, Shape09DtorMember, 1) \
	X(__is_trivially_destructible, Shape09DtorMember, 0) \
	X(__is_nothrow_destructible, Shape09DtorMember, 1) \
	X(__is_trivially_copyable, Shape09DtorMember, 0) \
	X(__is_trivial, Shape09DtorMember, 0) \
	X(__has_trivial_destructor, Shape09DtorMember, 0) \
	X(__has_virtual_destructor, Shape09DtorMember, 0) \
	X(__is_aggregate, Shape10DtorArrayMember, 1) \
	X(__is_pod, Shape10DtorArrayMember, 0) \
	X(__is_standard_layout, Shape10DtorArrayMember, 1) \
	X(__is_empty, Shape10DtorArrayMember, 0) \
	X(__is_polymorphic, Shape10DtorArrayMember, 0) \
	X(__is_final, Shape10DtorArrayMember, 0) \
	X(__is_abstract, Shape10DtorArrayMember, 0) \
	X(__is_destructible, Shape10DtorArrayMember, 1) \
	X(__is_trivially_destructible, Shape10DtorArrayMember, 0) \
	X(__is_nothrow_destructible, Shape10DtorArrayMember, 1) \
	X(__is_trivially_copyable, Shape10DtorArrayMember, 0) \
	X(__is_trivial, Shape10DtorArrayMember, 0) \
	X(__has_trivial_destructor, Shape10DtorArrayMember, 0) \
	X(__has_virtual_destructor, Shape10DtorArrayMember, 0) \
	X(__is_aggregate, Shape11VirtualFunction, 0) \
	X(__is_pod, Shape11VirtualFunction, 0) \
	X(__is_standard_layout, Shape11VirtualFunction, 0) \
	X(__is_empty, Shape11VirtualFunction, 0) \
	X(__is_polymorphic, Shape11VirtualFunction, 1) \
	X(__is_final, Shape11VirtualFunction, 0) \
	X(__is_abstract, Shape11VirtualFunction, 0) \
	X(__is_destructible, Shape11VirtualFunction, 1) \
	X(__is_trivially_destructible, Shape11VirtualFunction, 1) \
	X(__is_nothrow_destructible, Shape11VirtualFunction, 1) \
	X(__is_trivially_copyable, Shape11VirtualFunction, 0) \
	X(__is_trivial, Shape11VirtualFunction, 0) \
	X(__has_trivial_destructor, Shape11VirtualFunction, 1) \
	X(__has_virtual_destructor, Shape11VirtualFunction, 0) \
	X(__is_aggregate, Shape12VirtualDestructor, 0) \
	X(__is_pod, Shape12VirtualDestructor, 0) \
	X(__is_standard_layout, Shape12VirtualDestructor, 0) \
	X(__is_empty, Shape12VirtualDestructor, 0) \
	X(__is_polymorphic, Shape12VirtualDestructor, 1) \
	X(__is_final, Shape12VirtualDestructor, 0) \
	X(__is_abstract, Shape12VirtualDestructor, 0) \
	X(__is_destructible, Shape12VirtualDestructor, 1) \
	X(__is_trivially_destructible, Shape12VirtualDestructor, 0) \
	X(__is_nothrow_destructible, Shape12VirtualDestructor, 1) \
	X(__is_trivially_copyable, Shape12VirtualDestructor, 0) \
	X(__is_trivial, Shape12VirtualDestructor, 0) \
	X(__has_trivial_destructor, Shape12VirtualDestructor, 0) \
	X(__has_virtual_destructor, Shape12VirtualDestructor, 1) \
	X(__is_aggregate, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_pod, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_standard_layout, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_empty, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_polymorphic, Shape13VirtualDtorViaVirtualBase, 1) \
	X(__is_final, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_abstract, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_destructible, Shape13VirtualDtorViaVirtualBase, 1) \
	X(__is_trivially_destructible, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_nothrow_destructible, Shape13VirtualDtorViaVirtualBase, 1) \
	X(__is_trivially_copyable, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__is_trivial, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__has_trivial_destructor, Shape13VirtualDtorViaVirtualBase, 0) \
	X(__has_virtual_destructor, Shape13VirtualDtorViaVirtualBase, 1) \
	X(__is_aggregate, Shape14Abstract, 0) \
	X(__is_pod, Shape14Abstract, 0) \
	X(__is_standard_layout, Shape14Abstract, 0) \
	X(__is_empty, Shape14Abstract, 0) \
	X(__is_polymorphic, Shape14Abstract, 1) \
	X(__is_final, Shape14Abstract, 0) \
	X(__is_abstract, Shape14Abstract, 1) \
	X(__is_destructible, Shape14Abstract, 1) \
	X(__is_trivially_destructible, Shape14Abstract, 1) \
	X(__is_nothrow_destructible, Shape14Abstract, 1) \
	X(__is_trivially_copyable, Shape14Abstract, 0) \
	X(__is_trivial, Shape14Abstract, 0) \
	X(__has_trivial_destructor, Shape14Abstract, 1) \
	X(__has_virtual_destructor, Shape14Abstract, 0) \
	X(__is_aggregate, Shape15ScalarUnion, 1) \
	X(__is_pod, Shape15ScalarUnion, 1) \
	X(__is_standard_layout, Shape15ScalarUnion, 1) \
	X(__is_empty, Shape15ScalarUnion, 0) \
	X(__is_polymorphic, Shape15ScalarUnion, 0) \
	X(__is_final, Shape15ScalarUnion, 0) \
	X(__is_abstract, Shape15ScalarUnion, 0) \
	X(__is_destructible, Shape15ScalarUnion, 1) \
	X(__is_trivially_destructible, Shape15ScalarUnion, 1) \
	X(__is_nothrow_destructible, Shape15ScalarUnion, 1) \
	X(__is_trivially_copyable, Shape15ScalarUnion, 1) \
	X(__is_trivial, Shape15ScalarUnion, 1) \
	X(__has_trivial_destructor, Shape15ScalarUnion, 1) \
	X(__has_virtual_destructor, Shape15ScalarUnion, 0) \
	X(__is_aggregate, Shape16NontrivialUnion, 1) \
	X(__is_pod, Shape16NontrivialUnion, 0) \
	X(__is_standard_layout, Shape16NontrivialUnion, 1) \
	X(__is_empty, Shape16NontrivialUnion, 0) \
	X(__is_polymorphic, Shape16NontrivialUnion, 0) \
	X(__is_final, Shape16NontrivialUnion, 0) \
	X(__is_abstract, Shape16NontrivialUnion, 0) \
	X(__is_destructible, Shape16NontrivialUnion, 1) \
	X(__is_trivially_destructible, Shape16NontrivialUnion, 0) \
	X(__is_nothrow_destructible, Shape16NontrivialUnion, 1) \
	X(__is_trivially_copyable, Shape16NontrivialUnion, 0) \
	X(__is_trivial, Shape16NontrivialUnion, 0) \
	X(__has_trivial_destructor, Shape16NontrivialUnion, 0) \
	X(__has_virtual_destructor, Shape16NontrivialUnion, 0) \
	X(__is_aggregate, Shape17DerivedFromDataBase, 1) \
	X(__is_pod, Shape17DerivedFromDataBase, 1) \
	X(__is_standard_layout, Shape17DerivedFromDataBase, 1) \
	X(__is_empty, Shape17DerivedFromDataBase, 0) \
	X(__is_polymorphic, Shape17DerivedFromDataBase, 0) \
	X(__is_final, Shape17DerivedFromDataBase, 0) \
	X(__is_abstract, Shape17DerivedFromDataBase, 0) \
	X(__is_destructible, Shape17DerivedFromDataBase, 1) \
	X(__is_trivially_destructible, Shape17DerivedFromDataBase, 1) \
	X(__is_nothrow_destructible, Shape17DerivedFromDataBase, 1) \
	X(__is_trivially_copyable, Shape17DerivedFromDataBase, 1) \
	X(__is_trivial, Shape17DerivedFromDataBase, 1) \
	X(__has_trivial_destructor, Shape17DerivedFromDataBase, 1) \
	X(__has_virtual_destructor, Shape17DerivedFromDataBase, 0) \
	X(__is_aggregate, Shape18DerivedOwnData, 1) \
	X(__is_pod, Shape18DerivedOwnData, 0) \
	X(__is_standard_layout, Shape18DerivedOwnData, 0) \
	X(__is_empty, Shape18DerivedOwnData, 0) \
	X(__is_polymorphic, Shape18DerivedOwnData, 0) \
	X(__is_final, Shape18DerivedOwnData, 0) \
	X(__is_abstract, Shape18DerivedOwnData, 0) \
	X(__is_destructible, Shape18DerivedOwnData, 1) \
	X(__is_trivially_destructible, Shape18DerivedOwnData, 1) \
	X(__is_nothrow_destructible, Shape18DerivedOwnData, 1) \
	X(__is_trivially_copyable, Shape18DerivedOwnData, 1) \
	X(__is_trivial, Shape18DerivedOwnData, 1) \
	X(__has_trivial_destructor, Shape18DerivedOwnData, 1) \
	X(__has_virtual_destructor, Shape18DerivedOwnData, 0) \
	X(__is_aggregate, Shape19PrivateData, 1) \
	X(__is_pod, Shape19PrivateData, 0) \
	X(__is_standard_layout, Shape19PrivateData, 1) \
	X(__is_empty, Shape19PrivateData, 0) \
	X(__is_polymorphic, Shape19PrivateData, 0) \
	X(__is_final, Shape19PrivateData, 0) \
	X(__is_abstract, Shape19PrivateData, 0) \
	X(__is_destructible, Shape19PrivateData, 0) \
	X(__is_trivially_destructible, Shape19PrivateData, 0) \
	X(__is_nothrow_destructible, Shape19PrivateData, 0) \
	X(__is_trivially_copyable, Shape19PrivateData, 0) \
	X(__is_trivial, Shape19PrivateData, 0) \
	X(__has_trivial_destructor, Shape19PrivateData, 0) \
	X(__has_virtual_destructor, Shape19PrivateData, 0) \
	X(__is_aggregate, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_pod, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_standard_layout, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_empty, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_polymorphic, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_final, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_abstract, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_destructible, Shape20VirtualBaseNoVirtualFunctions, 1) \
	X(__is_trivially_destructible, Shape20VirtualBaseNoVirtualFunctions, 1) \
	X(__is_nothrow_destructible, Shape20VirtualBaseNoVirtualFunctions, 1) \
	X(__is_trivially_copyable, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__is_trivial, Shape20VirtualBaseNoVirtualFunctions, 0) \
	X(__has_trivial_destructor, Shape20VirtualBaseNoVirtualFunctions, 1) \
	X(__has_virtual_destructor, Shape20VirtualBaseNoVirtualFunctions, 0)

#define ASSERT_RECORD_PROPERTY(Trait, Shape, Expected) static_assert(Trait(Shape) == Expected, #Trait "(" #Shape ")");
RECORD_PROPERTY_MATRIX(ASSERT_RECORD_PROPERTY)

int main() {
	int mismatches = 0;
#define CHECK_RECORD_PROPERTY(Trait, Shape, Expected) { int value = Trait(Shape); mismatches += (value != Expected); }
	RECORD_PROPERTY_MATRIX(CHECK_RECORD_PROPERTY)
#undef CHECK_RECORD_PROPERTY
	return mismatches;
}
#undef ASSERT_RECORD_PROPERTY
#undef RECORD_PROPERTY_MATRIX
