#include <typeinfo>

struct Base {
	virtual ~Base() {}
};

struct Derived : Base {
};

int main() {
	Derived value;
	return typeid(Derived) == typeid(value) ? 0 : 1;
}
