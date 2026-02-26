// Test: out-of-line nested class definition with data members
// Verifies that template<T> struct Wrapper<T>::Nested { T value; } is properly parsed
// and the nested class members are accessible after instantiation
template<typename T>
struct Wrapper {
	struct Nested;
};

template<typename T>
struct Wrapper<T>::Nested {
	T value;
};

int main() {
	Wrapper<int>::Nested n;
	n.value = 42;
	return n.value;
}
