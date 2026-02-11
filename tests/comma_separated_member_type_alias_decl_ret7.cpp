// Test: comma-separated variable declarations using member type aliases in template bodies
// This verifies that type aliases defined in template struct bodies (e.g., using size_type = ...)
// are recognized when parsing member function bodies, and comma-separated declarations work correctly.

template<typename T>
struct Container {
	using size_type = unsigned long;
	using pointer = T*;

	size_type compute() {
		size_type a = 3, b = 4;
		return a + b;
	}
};

int main() {
	Container<int> c;
	return c.compute();
}
