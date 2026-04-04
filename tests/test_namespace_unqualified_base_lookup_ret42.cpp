// Test: unqualified base class lookup from nested namespace.
// Exercises the namespace-walk code in resolveStructInfo()
// where the base class is referenced without qualification
// from within the same namespace.
namespace outer {
	template <typename T>
	struct Base {
		T value;
	};

	namespace inner {
		struct Derived : Base<int> {
			int get() { return value; }
		};
	}
}

int main() {
	outer::inner::Derived d;
	d.value = 42;
	return d.get();
}
