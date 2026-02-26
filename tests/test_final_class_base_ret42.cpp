// Test: class with 'final' specifier before base class list
// C++ standard: class-key identifier final(opt) base-clause(opt) { ... }

struct Base {
	int x;
};

struct Derived final : public Base {
	int get_x() { return x; }
};

int main() {
	Derived d;
	d.x = 42;
	return d.get_x();
}
