// Test: constexpr constructor evaluation for static const struct members
// This tests that static members of struct types are properly initialized
// when using constexpr constructors with arguments.
struct Ordering {
	int val;
	constexpr explicit Ordering(int v) : val(v) {}
	static const Ordering less;
	static const Ordering equal;
	static const Ordering greater;
};

const Ordering Ordering::less = Ordering(-1);
const Ordering Ordering::equal = Ordering(0);
const Ordering Ordering::greater = Ordering(1);

int main() {
	int result = 0;

	Ordering r1 = Ordering::less;
	int v1 = r1.val;
	if (v1 < 0) result += 1;

	Ordering r2 = Ordering::equal;
	int v2 = r2.val;
	if (v2 == 0) result += 2;

	Ordering r3 = Ordering::greater;
	int v3 = r3.val;
	if (v3 > 0) result += 4;

	// result should be 1+2+4 = 7
	return result == 7 ? 42 : 0;
}
