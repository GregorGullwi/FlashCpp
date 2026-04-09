// Test that dependent template type args (Wrapper<T>::tag inside Outer<T>)
// and concrete args (Wrapper<int>::tag) produce distinct instantiations.
// Regression test for PR #1179: generateInstantiatedNameFromArgs() was
// collapsing type arguments that differed only by dependency state.

template <typename T>
struct Wrapper {
	static constexpr int tag = sizeof(T);
};

template <typename T>
struct Outer {
	static int getTag() { return Wrapper<T>::tag; }
};

int main() {
	int result = 0;
	// Concrete instantiation
	if (Wrapper<int>::tag != (int)sizeof(int)) result |= 1;
	if (Wrapper<double>::tag != (int)sizeof(double)) result |= 2;
	// Dependent instantiation through Outer<T>
	if (Outer<int>::getTag() != (int)sizeof(int)) result |= 4;
	if (Outer<double>::getTag() != (int)sizeof(double)) result |= 8;
	return result;
}
