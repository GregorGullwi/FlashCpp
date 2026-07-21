// Regression: nested classes must acquire their concrete semantic owner before
// constructor signatures substitute the injected class name.

template<typename T>
struct Outer {
	struct Inner {
		T current;

		explicit Inner(T value) : current(value) {}
		Inner(const Inner& other) : current(other.current) {}

		Inner copy_self() const {
			Inner copy = *this;
			return copy;
		}
	};
};

int main() {
	long long wide_value = 0x123456789LL;
	Outer<long long>::Inner wide_original(wide_value);
	Outer<long long>::Inner wide_copy = wide_original.copy_self();

	int narrow_value = 17;
	Outer<int>::Inner narrow_original(narrow_value);
	Outer<int>::Inner narrow_copy = narrow_original.copy_self();

	int result = 0;
	if (wide_copy.current != wide_value) result |= 1;
	if (narrow_copy.current != narrow_value) result |= 2;
	return result;
}
