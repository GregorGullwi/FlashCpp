// Constructor and data-member selection for a member class-template partial
// depends on the enclosing owner specialization and on the non-type argument.
// The specializations Owner<short>::Box<int*, 5> and
// Owner<long long>::Box<int*, 5> differ only in the owner argument, and
// Owner<short>::Box<int*, 7> differs from the first only in the non-type
// argument. Each identity must keep its own constructor and member schema.
// An equivalent alias spelling must resolve to the same identity, not publish
// a second one.

template <class T> struct Owner {
	template <class U, int N> struct Box {
		Box(long seed) : value(seed), tag(N) {}
		U* pointer;
		long value;
		int tag;
		short marker = 90;
	};
	template <class U> struct Box<U*, 5> {
		Box(T seed) : value(seed * 2), tag(5) {}
		Box(int) = delete;
		U* pointer;
		long value;
		int tag;
		int marker = 50;
	};
	template <class U> struct Box<U*, 7> {
		Box(int seed) : value(seed * 3), tag(7) {}
		U* pointer;
		char value;
		int tag;
		long marker = 70;
	};
};

using NarrowPointer = Owner<short>::Box<int*, 5>;
using NarrowPointerAgain = Owner<short>::Box<int*, 5>;
using WidePointer = Owner<long long>::Box<int*, 5>;
using SevenPointer = Owner<short>::Box<int*, 7>;

static_assert(!__is_same(NarrowPointer, WidePointer));
static_assert(!__is_same(NarrowPointer, SevenPointer));
static_assert(__is_same(NarrowPointer, NarrowPointerAgain));

static_assert(!__is_constructible(NarrowPointer, int));
static_assert(__is_constructible(NarrowPointer, short));
static_assert(__is_constructible(SevenPointer, int));
static_assert(sizeof(NarrowPointer::marker) == sizeof(int));
static_assert(sizeof(SevenPointer::marker) == sizeof(long));

int main() {
	int first = 1;
	int second = 2;

	NarrowPointer narrow_pointer(static_cast<short>(6));
	NarrowPointerAgain narrow_pointer_again(static_cast<short>(9));
	WidePointer wide_pointer(4LL);
	SevenPointer seven_pointer(5);

	narrow_pointer.pointer = &first;
	wide_pointer.pointer = &second;
	seven_pointer.pointer = &first;

	narrow_pointer.value = 11;
	wide_pointer.value = 13;
	seven_pointer.value = 17;

	NarrowPointerAgain& same_identity = narrow_pointer;
	same_identity.pointer = &second;
	same_identity.marker = 51;

	int result = 0;
	if (narrow_pointer.tag != 5) {
		result |= 1;
	}
	if (wide_pointer.tag != 5) {
		result |= 2;
	}
	if (seven_pointer.tag != 7) {
		result |= 4;
	}
	if (narrow_pointer.value != 11) {
		result |= 8;
	}
	if (wide_pointer.value != 13) {
		result |= 16;
	}
	if (seven_pointer.value != 17) {
		result |= 32;
	}
	if (narrow_pointer.pointer != &second) {
		result |= 64;
	}
	if (same_identity.pointer != narrow_pointer.pointer) {
		result |= 128;
	}
	if (narrow_pointer_again.marker != 50) {
		result |= 256;
	}
	if (narrow_pointer.marker != 51) {
		result |= 512;
	}
	if (wide_pointer.marker != 50) {
		result |= 1024;
	}
	if (seven_pointer.marker != 70) {
		result |= 2048;
	}
	if (*wide_pointer.pointer != 2) {
		result |= 4096;
	}
	return result;
}
