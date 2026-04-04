// C++20 requires a constexpr variable initializer to be a constant expression.
// Calling a non-constexpr function here is ill-formed.

int runtime_value() {
	return 42;
}

constexpr int value = runtime_value();

int main() {
	return value;
}
