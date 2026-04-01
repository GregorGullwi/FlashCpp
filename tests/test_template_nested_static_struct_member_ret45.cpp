// Test for the KNOWN_ISSUES.md bug: nested template static members of struct
// type reading back as zero-initialized.  Fixed by:
// 1. Setting current_struct_name_ in generateTrivialDefaultConstructors so
//    unqualified static member references resolve correctly.
// 2. Updating resolveGlobalOrStaticBinding to use the instantiated struct name
//    instead of the template pattern name.
template<typename U, class C, int N>
struct Outer {
	struct Payload { int a; int b; };
	struct Inner {
		static constexpr Payload payload = { static_cast<int>(sizeof(C) - sizeof(U)), N };
		int value = payload.a + payload.b;
	};
};

int main() {
	Outer<char, int, 39>::Inner inner{};
	if (inner.value != 42) return 1;
	if (Outer<char, int, 39>::Inner::payload.a != 3) return 2;
	if (Outer<char, int, 39>::Inner::payload.b != 39) return 3;
	return inner.value + Outer<char, int, 39>::Inner::payload.a;  // 42 + 3 = 45
}
