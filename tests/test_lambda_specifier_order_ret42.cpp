// Test lambda specifiers in non-standard order (C++20 lambda-specifier-seq)
// constexpr before mutable should be accepted
// Expected return: 42

int main() {
	auto f = [x = 10]() constexpr mutable -> int { x += 32; return x; };
	return f();
}
