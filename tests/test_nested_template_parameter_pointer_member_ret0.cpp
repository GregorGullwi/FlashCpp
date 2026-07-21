// Regression: pointer metadata from an outer template argument must survive
// member production for a nested class.

template<typename T>
struct Outer {
	struct Inner {
		T current;

		explicit Inner(T value) : current(value) {}
	};
};

int main() {
	int narrow = 17;
	long long wide = 0x123456789LL;
	Outer<int*>::Inner narrow_pointer(&narrow);
	Outer<long long*>::Inner wide_pointer(&wide);

	int result = 0;
	if (sizeof(narrow_pointer) != sizeof(int*)) result |= 1;
	if (sizeof(wide_pointer) != sizeof(long long*)) result |= 2;
	if (narrow_pointer.current != &narrow) result |= 4;
	if (wide_pointer.current != &wide) result |= 8;
	return result;
}
