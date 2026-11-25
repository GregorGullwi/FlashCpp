// Regression: a data-member pointer compares against nullptr by the all-bits-one
// null sentinel, so an offset-zero member pointer is still non-null. Every case
// uses a distinct return code so a regression identifies the operator form.
struct D {
	int first;
	int second;
};

struct Prefix {
	long long pad;
};

struct Derived : Prefix, D {
	int third;
};

struct Holder {
	int D::* pointer;
};

template <class T>
T value_initialized() {
	return T();
}

int take_member(int D::* pointer) {
	return pointer == nullptr ? 0 : 1;
}

int D::* return_null() {
	return nullptr;
}

int D::* return_zero() {
	return 0;
}

int D::* global_null = nullptr;
int D::* global_first = &D::first;
int D::* global_uninitialized;

constexpr int D::* constexpr_first = &D::first;
constexpr int D::* constexpr_null = nullptr;
static_assert(constexpr_first != nullptr, "offset-zero member pointer is not null");
static_assert(!(constexpr_first == nullptr), "offset-zero member pointer is not null");
static_assert(constexpr_null == nullptr, "null member pointer compares equal to nullptr");
static_assert(!(constexpr_null != nullptr), "null member pointer compares equal to nullptr");

int main() {
	int D::* first = &D::first;
	int D::* second = &D::second;
	int D::* null_data = nullptr;
	if (first == nullptr) return 1;
	if (second == nullptr) return 2;
	if (!(null_data == nullptr)) return 3;
	if (nullptr == first) return 4;
	if (nullptr == second) return 5;
	if (!(nullptr == null_data)) return 6;
	if (!(first != nullptr)) return 7;
	if (!(second != nullptr)) return 8;
	if (null_data != nullptr) return 9;
	if (!(nullptr != first)) return 10;
	if (!(nullptr != second)) return 11;
	if (nullptr != null_data) return 12;
	if (first == second) return 13;
	if (null_data == first) return 14;
	if (0 == first) return 15;
	if (!(first != 0)) return 16;

	// A derived-class pointer-to-member stores an owner-relative offset. The
	// Itanium ABI adds the base subobject offset during the base-to-derived
	// conversion, so both the converted offset-zero member and the inherited
	// member remain non-null.
	int D::* converted = &D::first;
	int Derived::* inherited_first = &Derived::first;
	int Derived::* inherited_third = &Derived::third;
	int Derived::* converted_derived = converted;
	if (converted == nullptr) return 17;
	if (inherited_first == nullptr) return 18;
	if (inherited_third == nullptr) return 19;
	if (nullptr == converted) return 20;
	if (!(inherited_first != nullptr)) return 21;
	if (converted_derived == nullptr) return 25;
	if (!(nullptr != converted_derived)) return 26;

	Derived object{};
	object.first = 41;
	object.second = 0;
	object.third = 0;
	if (object.*inherited_first != 41) return 22;
	if (object.*converted != 41) return 23;
	int D::* chosen = object.second == 0 ? &D::second : &D::first;
	if (chosen == nullptr) return 24;

	// Every null producer must agree with the sentinel: value-initialization,
	// static_cast/reinterpret_cast of a null pointer constant, an integer zero
	// initializer, assignment, return, and argument passing.
	int D::* value_null = value_initialized<int D::*>();
	int D::* cast_null = static_cast<int D::*>(nullptr);
	int D::* integer_cast_null = static_cast<int D::*>(0);
	int D::* reinterpret_null = reinterpret_cast<int D::*>(nullptr);
	int D::* integer_null = 0;
	int D::* brace_null{};
	if (value_null != nullptr) return 27;
	if (!(nullptr == value_null)) return 28;
	if (cast_null != nullptr) return 29;
	if (cast_null == &D::first) return 30;
	if (integer_cast_null != nullptr) return 31;
	if (reinterpret_null != nullptr) return 32;
	if (integer_null != nullptr) return 33;
	if (brace_null != nullptr) return 34;
	int D::* assigned = &D::first;
	assigned = nullptr;
	if (assigned != nullptr) return 35;
	assigned = 0;
	if (assigned != nullptr) return 36;
	if (return_null() != nullptr) return 37;
	if (return_zero() != nullptr) return 38;
	if (take_member(nullptr) != 0) return 39;
	if (take_member(0) != 0) return 40;
	if (global_null != nullptr) return 41;
	if (!(nullptr == global_null)) return 42;
	if (global_first == nullptr) return 43;
	static int D::* static_uninitialized;
	if (static_uninitialized != nullptr) return 44;
	if (global_uninitialized != nullptr) return 45;
	Holder holder{};
	if (holder.pointer != nullptr) return 46;
	return 0;
}
