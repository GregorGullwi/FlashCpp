template<class T> struct Base { T value; };
struct Leaf : Base<short> {};
struct Padding { long long guard; };
struct Offset : Padding, Base<short> { int tag; };
struct Holder { Leaf child; };
struct ConstructedBase {
	int value;
	ConstructedBase(int input) : value(input) {}
};
struct Mixed : ConstructedBase { short tag; };
struct WithDefault : Base<short> { static constexpr int seed = 47; int tag = seed; };
struct CopyBase { short value; };
struct Copyable : CopyBase { int tag; };
struct PointerHolder : Base<short> { int* pointer; };
int main() {
	Leaf leaf{{13}};
	if (leaf.value != 13) return 1;
	Offset offset{{12345678901LL}, {17}, 19};
	if (offset.guard != 12345678901LL || offset.value != 17 || offset.tag != 19) return 2;
	Holder holder{{{23}}};
	if (holder.child.value != 23) return 3;
	Leaf array[2] = {{{29}}, {{-31}}};
	if (array[0].value != 29 || array[1].value != -31) return 4;
	Mixed mixed{{37}, 41};
	if (mixed.value != 37 || mixed.tag != 41) return 5;
	Holder nested_array[1] = {{{{43}}}};
	if (nested_array[0].child.value != 43) return 6;
	Copyable source{{53}, 59};
	Copyable copied{source};
	if (copied.value != 53 || copied.tag != 59) return 7;
	Copyable moved{static_cast<Copyable&&>(copied)};
	if (moved.value != 53 || moved.tag != 59) return 8;
	WithDefault defaulted{{49}};
	if (defaulted.value != 49 || defaulted.tag != 47) return 9;
	int values[2] = {61, 67};
	PointerHolder pointer{{71}, values};
	if (pointer.value != 71 || pointer.pointer != values) return 10;
	PointerHolder braced_pointer{{73}, {values}};
	if (braced_pointer.value != 73 || braced_pointer.pointer != values) return 11;
	return 0;
}
