// Test: Pointer-type member with brace default member initializer inside a
// member struct template PARTIAL SPECIALIZATION body. This mirrors the
// transform_view::_Iterator pattern but on the partial-specialization path
// of parse_member_struct_template, which previously fell into the fallback
// "Skip other complex cases" else block and silently dropped the member.
//
//   template<typename T> struct Outer::Inner<T*> { int* ptr{}; };
//
// The test verifies the members are actually registered by checking that
// the partial specialization has a non-zero size that accounts for both
// the pointer and the dependent type member.

struct outer {
	template <typename T>
	struct inner {};

	template <typename T>
	struct inner<T*> {
		int* ptr{};
		T value{};
	};
};

int main() {
	// If the fallback else block silently dropped both members, the partial
	// specialization would be an empty struct (size 1). With both members
	// registered, it must be at least sizeof(int*) + sizeof(int).
	static_assert(sizeof(outer::inner<int*>) >= sizeof(int*) + sizeof(int),
		"partial specialization members with brace-init were dropped");
	return 0;
}