// A direct Base(Derived) converting constructor must win over Base's copy
// constructor. Derived-to-base slicing is only the fallback when no better
// user-defined constructor is viable.

struct Derived;

struct Base {
	int value;

	Base();
	Base(const Base& other);
	Base(const Derived& source);
};

struct Derived : Base {
	Derived() : Base() {}
};

Base::Base() : value(7) {}
Base::Base(const Base& other) : value(other.value + 100) {}
Base::Base(const Derived& source) : value(200) {}

int main() {
	Derived source;
	Base result = source;
	return result.value == 200 ? 0 : 1;
}
