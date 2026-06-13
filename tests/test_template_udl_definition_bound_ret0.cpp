// Test: non-dependent user-defined literal calls inside template bodies stay
// definition-bound through instantiation.

inline constexpr int operator""_len(const char* str, unsigned long len) noexcept {
	return static_cast<int>(len);
}

template <typename T>
constexpr int literalLength() {
	return "hello"_len;
}

int main() {
	static_assert(literalLength<int>() == 5);
	return literalLength<int>() - 5;
}
