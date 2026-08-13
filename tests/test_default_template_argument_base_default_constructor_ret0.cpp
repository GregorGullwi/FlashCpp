template<typename T>
struct Base {
	template<typename Dummy = T>
	Base() : value(sizeof(Dummy)) {}

	int value;
};

struct Derived : Base<int> {
	Derived() {}
};

int main() {
	Derived value;
	return value.value == sizeof(int) ? 0 : 1;
}
