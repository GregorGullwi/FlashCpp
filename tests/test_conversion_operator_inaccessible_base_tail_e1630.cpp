// A conversion operator returning a derived class whose base is inaccessible
// cannot be used to initialize the base; the trailing derived-to-base step is
// rejected with InaccessibleDerivedToBaseConversion.

struct Base {};

struct Middle : private Base {};

struct Source {
	operator Middle() {
		return Middle{};
	}
};

int main() {
	Base value = Source{};
	return 0;
}
