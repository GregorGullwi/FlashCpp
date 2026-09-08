// Dependent noexcept(expr) is retained on template function types until
// instantiation. Canonical Function identity carries an ExprId for that form.
template <typename T>
struct Box {
	T value;
};

template <typename T>
int measure() noexcept(sizeof(T) > 1) {
	return static_cast<int>(sizeof(T));
}

template <typename T>
int invoke(int (*fn)() noexcept(sizeof(T) > 1)) {
	return fn();
}

int main() {
	return invoke<char>(&measure<char>) +
		invoke<Box<int>>(&measure<Box<int>>) -
		(1 + static_cast<int>(sizeof(Box<int>)));
}
