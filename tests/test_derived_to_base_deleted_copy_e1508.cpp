// A deleted Base copy constructor makes derived-to-base object initialization
// ill-formed; the conversion must not fall back to a raw byte copy.

struct Base {
	Base() {}
	Base(const Base&) = delete;
};

struct Derived : Base {};

int main() {
	Derived source;
	Base copy = source;
	return 0;
}
