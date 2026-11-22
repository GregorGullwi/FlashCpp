// C++20 permits pointers to array elements in structural template arguments.
// Both the array declaration and the element offset contribute to identity.
int first_array[2] = { 1, 2 };
int second_array[2] = { 3, 4 };
int (&array_reference)[2] = first_array;

struct Key {
	int* pointer;
	int tag;
};

constexpr Key first{&first_array[0], 1};
constexpr Key first_again{&first_array[0], 1};
constexpr Key next{&first_array[1], 1};
constexpr Key other{&second_array[0], 1};
constexpr Key through_reference{&array_reference[1], 1};

template<class A, class B> struct Same { static constexpr bool value = false; };
template<class A> struct Same<A, A> { static constexpr bool value = true; };
template<Key Value> struct Holder { int marker; };

int main() {
	int result = 0;
	if (!Same<Holder<first>, Holder<first_again>>::value) {
		result |= 1;
	}
	if (Same<Holder<first>, Holder<next>>::value) {
		result |= 2;
	}
	if (Same<Holder<first>, Holder<other>>::value) {
		result |= 4;
	}
	if (!Same<Holder<next>, Holder<through_reference>>::value) {
		result |= 8;
	}
	return result;
}
