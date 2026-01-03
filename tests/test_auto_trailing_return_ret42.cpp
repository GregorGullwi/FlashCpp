// Test auto return type with trailing return type in template function

template<typename T>
struct Helper {
    using type = int;
};

// Function with auto and trailing return type
auto test_func(int x) -> Helper<void>::type {
	return x + 42;
}

int main() {
	return test_func(0);  // Should return 42
}
