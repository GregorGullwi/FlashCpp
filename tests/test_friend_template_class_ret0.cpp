// Test that template friend class declarations grant access to private members.
// C++ allows: template<typename T> friend struct Foo;
// Any instantiation of Foo<T> should be able to access the class's private members.
struct Outer {
private:
	int secret;

public:
	Outer() : secret(42) {}
	template <typename T>
	friend struct Accessor;
};

template <typename T>
struct Accessor {
	int get(const Outer& o) { return o.secret; }
};

int main() {
	Outer o;
	Accessor<int> a;
	return a.get(o) == 42 ? 0 : 1;
}
