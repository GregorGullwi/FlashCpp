// Test: Destructor explicitly marked noexcept(false) is NOT noexcept.
// noexcept(T{}.~T()) should return false for such a type.

struct Throwing {
	int value;
	~Throwing() noexcept(false) {}  // explicitly may throw
};

struct Normal {
	int value;
	~Normal() {}  // implicitly noexcept(true)
};

int main() {
	int result = 0;

	// Throwing destructor should NOT be noexcept
	if (noexcept(Throwing{}.~Throwing()))
		result |= 1;

	// Normal destructor SHOULD be noexcept
	if (!noexcept(Normal{}.~Normal()))
		result |= 2;

	return result;
}
