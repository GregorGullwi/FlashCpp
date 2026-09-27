struct MutableReferenceSelection {};
struct ConstReferenceSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

MutableReferenceSelection choose(const int*&);
ConstReferenceSelection choose(const int* const&);

int* integerPointer;

static_assert(SameType<
	decltype(choose(integerPointer)), ConstReferenceSelection>::value);

int main() {
	return 0;
}
