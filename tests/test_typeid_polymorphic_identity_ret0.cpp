#include <typeinfo>

struct Base {
	virtual ~Base() {}
};

struct Derived : Base {
};

int main() {
	Derived value;
	const void* from_type = typeid(Derived);
	const void* from_expr = typeid(value);
	return from_type == from_expr ? 0 : 1;
}
