// Test: const char* with constexpr functions.
//
// Covers:
//  - constexpr function returning a string literal
//  - compile-time array subscript on constexpr const char* (static_assert)
//  - runtime subscript on a non-constexpr const char* obtained from a constexpr function
//  - compile-time string length via constexpr function

constexpr const char* get_hello() {
	return "Hello";
}

constexpr const char* get_empty() {
	return "";
}

// Compute string length at compile time
constexpr int str_len(const char* s) {
	int len = 0;
	while (s[len] != '\0')
		++len;
	return len;
}

// compile-time evaluation: constexpr const char* subscript
constexpr const char* hello = get_hello();
static_assert(hello[0] == 'H', "hello[0] should be H");
static_assert(hello[4] == 'o', "hello[4] should be o");

// compile-time string length
constexpr int hello_len = str_len("Hello");
static_assert(hello_len == 5, "hello_len should be 5");

constexpr int empty_len = str_len("");
static_assert(empty_len == 0, "empty_len should be 0");

int main() {
	// Case 1: non-constexpr const char* from constexpr function (runtime call)
	const char* s = get_hello();
	if (s[0] != 'H')
		return 1;
	if (s[1] != 'e')
		return 2;
	if (s[4] != 'o')
		return 3;

	// Case 2: empty string pointer
	const char* empty = get_empty();
	if (empty[0] != '\0')
		return 4;

	// Case 3: runtime string length via constexpr function
	int len = str_len("World");
	if (len != 5)
		return 5;

	int len2 = str_len(s);  // runtime: s = "Hello"
	if (len2 != 5)
		return 6;

	return 0;
}
