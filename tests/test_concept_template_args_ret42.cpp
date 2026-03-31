// Test that concept template arguments are parsed correctly
// Concept<U> T pattern should work

template <typename T, typename U>
concept ConvertibleTo = requires(T t) {
	static_cast<U>(t);
};

template <ConvertibleTo<int> T>
int convert(T value) {
	return static_cast<int>(value);
}

int main() {
	return convert(42.0);  // double is convertible to int
}
