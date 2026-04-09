// Regression test for dependent template arguments that add pointer modifiers
// to an outer template parameter before member access.

template <typename T>
struct Wrapper {
	static constexpr int tag = sizeof(T);
};

template <typename T>
struct Outer {
	static int getTag() { return Wrapper<T>::tag; }
	static int getPointerTag() { return Wrapper<T*>::tag; }
};

template <typename T>
struct RefWrapper {
	static constexpr int kind = 0;
};

template <typename T>
struct RefWrapper<T&> {
	static constexpr int kind = 1;
};

template <typename T>
struct RefWrapper<T&&> {
	static constexpr int kind = 2;
};

template <typename T>
struct PointerCvWrapper {
	static constexpr int kind = 0;
};

template <typename T>
struct PointerCvWrapper<T* const> {
	static constexpr int kind = 1;
};

template <typename T>
struct ModifierOuter {
	static int getLRefKind() { return RefWrapper<T&>::kind; }
	static int getRRefKind() { return RefWrapper<T&&>::kind; }
	static int getConstPointerKind() { return PointerCvWrapper<T* const>::kind; }
};

int main() {
	enum ResultBits {
		WrongOuterIntTag = 1,
		WrongOuterDoubleTag = 2,
		WrongWrapperIntTag = 4,
		WrongWrapperDoubleTag = 8,
		WrongOuterIntPointerTag = 16,
		WrongOuterDoublePointerTag = 32,
		WrongLRefKind = 64,
		WrongRRefKind = 128,
		WrongConstPointerKind = 256,
	};

	int result = 0;
	if (Outer<int>::getTag() != (int)sizeof(int)) result |= WrongOuterIntTag;
	if (Outer<double>::getTag() != (int)sizeof(double)) result |= WrongOuterDoubleTag;
	if (Wrapper<int>::tag != (int)sizeof(int)) result |= WrongWrapperIntTag;
	if (Wrapper<double>::tag != (int)sizeof(double)) result |= WrongWrapperDoubleTag;
	if (Outer<int>::getPointerTag() != (int)sizeof(int*)) result |= WrongOuterIntPointerTag;
	if (Outer<double>::getPointerTag() != (int)sizeof(double*)) result |= WrongOuterDoublePointerTag;
	if (ModifierOuter<int>::getLRefKind() != 1) result |= WrongLRefKind;
	if (ModifierOuter<int>::getRRefKind() != 2) result |= WrongRRefKind;
	if (ModifierOuter<int>::getConstPointerKind() != 1) result |= WrongConstPointerKind;
	return result;
}
