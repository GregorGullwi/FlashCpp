// Test: constexpr operator[] picks the const overload when both const and
// non-const overloads are present on a struct (was previously ambiguous).

// Case 1: simple int-member struct at global scope
struct Wrap {
	int val;
	constexpr int operator[](int) const { return val; }
	int& operator[](int) { return val; }
};
constexpr Wrap w{42};
static_assert(w[0] == 42);

// Case 2: char buffer struct at global scope
struct MyBuf {
	char data[8];
	int size;
	constexpr char operator[](int i) const { return data[i]; }
	char& operator[](int i) { return data[i]; }
};
constexpr MyBuf s{{'h', 'e', 'l', 'l', 'o', 0, 0, 0}, 5};
static_assert(s[0] == 'h');
static_assert(s[1] == 'e');

// Case 3: inside a constexpr function
constexpr int func_test() {
	Wrap w2{99};
	return w2[0];
}
static_assert(func_test() == 99);

int main() { return 0; }
