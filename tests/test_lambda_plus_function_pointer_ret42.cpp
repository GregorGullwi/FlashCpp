int call_fp(int (*fp)(int)) {
	return fp(41);
}

int g_val = 0;

void call_void_fp(void (*fp)(int)) {
	fp(10);
}

int main() {
	// Test 1: unary + on lambda with int return (deduced from return statement)
	auto fp = +[](int v) { return v + 1; };
	int result = call_fp(fp);  // 42

	// Test 2: unary + on void-returning lambda (no return statements => void deduction)
	auto vfp = +[](int v) { g_val = v; };
	call_void_fp(vfp);
	// g_val should be 10; subtract it so final result stays 42
	result = result + g_val - 10;  // 42 + 10 - 10 = 42

	return result;
}
