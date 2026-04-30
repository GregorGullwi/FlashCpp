// Regression test for `computeTemplateTypeArgSizeBits` (src/TemplateRegistry_Types.h)
// — array-of-struct variant.
//
// This is the layout-sensitive companion to test_template_struct_arg_sizeof_ret0.cpp.
// Embedding `T arr[N]` inside the template forces the substituted
// TypeSpecifierNode's per-element size to flow into array layout: if
// `computeTemplateTypeArgSizeBits` ever returns `0` for a complete struct
// argument, `sizeof(Holder<T>)` collapses and pointer arithmetic on the array
// silently aliases neighbouring elements.
//
// A `static_assert` alone would not catch that regression at the codegen level,
// so this test also writes distinct values through each array slot and reads
// them back to verify the stride.

struct Vec3 {
	int x;
	int y;
	int z;
};

template <typename T, int N>
struct Holder {
	T items[N];
};

// Per-element size and total array size must reflect the real struct layout.
static_assert(sizeof(Holder<Vec3, 4>) >= sizeof(Vec3) * 4);
static_assert(sizeof(Holder<Vec3, 4>::items) == sizeof(Vec3) * 4);

int main() {
	Holder<Vec3, 3> h{};

	// Distinct values per slot — if the per-element stride is wrong (e.g. the
	// substituted size was `0` and the array effectively collapsed), later
	// writes will overwrite earlier slots and the sum check below will fail.
	h.items[0] = Vec3{1, 2, 3};
	h.items[1] = Vec3{10, 20, 30};
	h.items[2] = Vec3{100, 200, 300};

	const int sum =
		h.items[0].x + h.items[0].y + h.items[0].z +
		h.items[1].x + h.items[1].y + h.items[1].z +
		h.items[2].x + h.items[2].y + h.items[2].z;

	// 1+2+3 + 10+20+30 + 100+200+300 = 666
	return sum == 666 ? 0 : 1;
}
