// Copy-initializing a class from a derived object whose base is reached through
// two inheritance paths is an ambiguous derived-to-base conversion and must
// report AmbiguousDerivedToBaseConversion.

struct Base {};

struct Left : Base {};

struct Right : Base {};

struct Diamond : Left, Right {};

int main() {
	Base value = Diamond{};
	return 0;
}
