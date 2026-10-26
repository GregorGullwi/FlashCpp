// Access control applies after overload ranking selects a conversion operator
// for a reference call argument.
struct Base {
	int value;
};

struct Derived : Base {};

struct Source {
private:
	Derived derived;
	operator Derived&() { return derived; }
};

int take(const Base& value) { return value.value; }

int main() {
	Source source;
	return take(source);
}
