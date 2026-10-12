// Copy-initializing a base object from a derived object whose base is private is
// an inaccessible derived-to-base conversion and must report
// InaccessibleDerivedToBaseConversion.

struct Base {};

struct Derived : private Base {};

int main() {
	Base value = Derived{};
	return 0;
}
