// Access control applies after brace reference initialization selects a
// conversion operator.
struct Base {
	int value;
};

struct Derived : Base {};

struct Source {
private:
	Derived derived;
	operator Derived&() { return derived; }
};

int main() {
	Source source;
	const Base& reference{source};
	return reference.value;
}
