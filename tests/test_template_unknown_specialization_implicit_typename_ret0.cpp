// C++20 P0634R3: omitted `typename` is valid in a member-declaration even
// when the qualified name is an unknown specialization through a dependent base.

template <typename T>
struct Base {
	using type = int;
};

template <typename T>
struct Mid : Base<T> {
};

template <typename T>
struct Derived : Mid<T> {
	Mid<T>::type value{};

	int run() {
		value = 42;
		return value - 42;
	}
};

int main() {
	Derived<int> d;
	Derived<long long> e;
	e.value = 1;
	return d.run() + static_cast<int>(e.value) - 1;
}
