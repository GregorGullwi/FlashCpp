// Regression test for namespace-aware type lookup in resolveStructInfo().
// Exercises unqualified base class lookup from a nested namespace,
// forcing the namespace walk to find the base in a parent namespace.
// Returns 0 on success, non-zero exit codes identify which sub-test failed.
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
	if (d.get() != 42) return 1;
	if (d.value != 42) return 2;
	return 0;
}
