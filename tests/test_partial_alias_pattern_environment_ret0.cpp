// An alias-template-id in a partial-specialization argument denotes its
// aliased type. The pattern must be matched and published through the expanded
// target (T*), so a pointer specialization selects for int* and the primary
// stays selected for a shape the alias does not produce. A second alias with
// the same target must reach the same pattern identity.

template <class T>
using Ptr = T*;

template <class T>
using EquivalentPtr = T*;

template <class T>
using Pair = T[2];

template <class T>
struct Box {
	static constexpr int value = 0;
	static constexpr int width = 0;
};

template <class T>
struct Box<Ptr<T>> {
	static constexpr int value = 1;
	static constexpr int width = sizeof(T);
};

template <class T>
struct Box<Pair<T>> {
	static constexpr int value = 2;
	static constexpr int width = sizeof(T);
};

struct Payload {
	int member;
	char tag;
};

static_assert(Box<int*>::value == 1);
static_assert(Box<int*>::width == static_cast<int>(sizeof(int)));
static_assert(Box<long*>::value == 1);
static_assert(Box<long*>::width == static_cast<int>(sizeof(long)));
static_assert(Box<EquivalentPtr<int>>::value == 1);
static_assert(Box<Payload*>::value == 1);
static_assert(Box<Payload*>::width == static_cast<int>(sizeof(Payload)));
static_assert(Box<int[2]>::value == 2);
static_assert(Box<char[2]>::value == 2);
static_assert(Box<unsigned[2]>::width == static_cast<int>(sizeof(unsigned)));

// A different alias target and a different argument both stay on the primary.
static_assert(Box<int>::value == 0);
static_assert(Box<int[3]>::value == 0);
static_assert(Box<char&>::value == 0);

int main() {
	const Box<int*> pointer;
	const Box<EquivalentPtr<long>> equivalent;
	const Box<Payload*> record_pointer;
	const Box<int[2]> array;
	const Box<int> primary;
	const Box<int[3]> other_array;
	if (pointer.value != 1 || pointer.width != static_cast<int>(sizeof(int))) {
		return 1;
	}
	if (equivalent.value != 1 || equivalent.width != static_cast<int>(sizeof(long))) {
		return 2;
	}
	if (record_pointer.value != 1 || record_pointer.width != static_cast<int>(sizeof(Payload))) {
		return 3;
	}
	if (array.value != 2 || array.width != static_cast<int>(sizeof(int))) {
		return 4;
	}
	if (primary.value != 0 || primary.width != 0) {
		return 5;
	}
	if (other_array.value != 0 || other_array.width != 0) {
		return 6;
	}
	return 0;
}
