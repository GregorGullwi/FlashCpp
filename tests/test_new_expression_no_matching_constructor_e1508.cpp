// A new-expression whose braced arguments match no constructor must report the
// NoMatchingConstructor owner. The aggregate fallback cannot absorb the extra
// argument, so it reaches the missing-constructor path.

struct RequiresTwoInts {
	RequiresTwoInts(int, int) {}
};

int main() {
	RequiresTwoInts* pointer = new RequiresTwoInts{1, 2, 3};
	delete pointer;
	return 0;
}
