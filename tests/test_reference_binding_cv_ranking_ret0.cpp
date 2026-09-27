struct MutableSelection {};
struct ConstSelection {};
struct MutablePointerSelection {};
struct ConstPointerSelection {};
struct RvalueSelection {};
struct ConstReferenceSelection {};

template <class Left, class Right>
struct SameType {
	static constexpr bool value = false;
};

template <class Type>
struct SameType<Type, Type> {
	static constexpr bool value = true;
};

MutableSelection pick(int&);
ConstSelection pick(int const&);

MutablePointerSelection pickPointer(int*&);
ConstPointerSelection pickPointer(int* const&);

RvalueSelection pickRvalue(int&&);
ConstReferenceSelection pickRvalue(int const&);

int mutableInt;
int* pointer;

static_assert(SameType<decltype(pick(mutableInt)), MutableSelection>::value);
static_assert(SameType<
	decltype(pickPointer(pointer)), MutablePointerSelection>::value);
static_assert(SameType<decltype(pickRvalue(42)), RvalueSelection>::value);

int main() {
	return 0;
}
