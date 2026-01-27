// Test constexpr lambda support

extern "C" int puts(const char* str);

int main() {
	// Test 1: Simple constexpr lambda with no captures
	constexpr auto add = [](int a, int b) { return a + b; };
	constexpr int sum = add(3, 4);
	
	// sum should be 7 at compile time
	static_assert(sum == 7, "Lambda add failed");
	
	if (sum == 7) {
		puts("PASS: constexpr lambda add(3, 4) == 7");
	} else {
		puts("FAIL: constexpr lambda add returned wrong value");
		return 1;
	}
	
	// Test 2: Lambda used in array size (requires compile-time evaluation)
	constexpr auto get_size = []() { return 5; };
	int arr[get_size()];
	
	if (sizeof(arr) == 5 * sizeof(int)) {
		puts("PASS: constexpr lambda used as array size");
	} else {
		puts("FAIL: constexpr lambda array size incorrect");
		return 1;
	}
	
	// Test 3: Lambda with explicit capture by value
	constexpr int multiplier = 10;
	constexpr auto times_ten = [multiplier](int x) { return x * multiplier; };
	constexpr int result3 = times_ten(5);
	
	if (result3 == 50) {
		puts("PASS: constexpr lambda with capture [multiplier](5) == 50");
	} else {
		puts("FAIL: constexpr lambda with capture returned wrong value");
		return 1;
	}
	
	// Test 4: Lambda with multiple captures
	constexpr int base = 100;
	constexpr int offset = 7;
	constexpr auto compute = [base, offset](int x) { return base + x + offset; };
	constexpr int result4 = compute(3);
	
	if (result4 == 110) {
		puts("PASS: constexpr lambda with multiple captures == 110");
	} else {
		puts("FAIL: constexpr lambda with multiple captures returned wrong value");
		return 1;
	}
	
	puts("All constexpr lambda tests passed!");
	return 0;
}
