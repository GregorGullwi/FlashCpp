template <class T>
struct Self {
	T value;

	char operator+(const Self& other) const {
		return static_cast<char>(value) + static_cast<char>(other.value);
	}

	int operator+(const Self*) const {
		return 42;
	}

	Self& operator+=(const Self& other) {
		value = static_cast<T>(value + other.value);
		return *this;
	}
};

int main() {
	Self<int> left{42};
	Self<int> right{0};
	if (static_cast<int>(left + right) != 42) {
		return 1;
	}
	if (left + &right != 42) {
		return 2;
	}
	Self<int>& result = (left += right);
	if (result.value != 42) {
		return 3;
	}
	result.value = 43;
	return left.value == 43 ? 0 : 4;
}
