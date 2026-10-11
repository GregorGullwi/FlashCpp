template <class T>
struct Self {
	T value;

	char operator+(const Self& other) const {
		return static_cast<char>(value) + static_cast<char>(other.value);
	}

	int operator+(const Self*) const {
		return 42;
	}
};

int main() {
	Self<int> left{42};
	Self<int> right{0};
	if (static_cast<int>(left + right) != 42) {
		return 1;
	}
	return left + &right == 42 ? 0 : 2;
}
