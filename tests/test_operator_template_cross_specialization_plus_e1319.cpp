template <class T>
struct Self {
	T value;

	int operator+(const Self&) const {
		return static_cast<int>(value);
	}
};

int main() {
	Self<int> left{42};
	Self<char> right{0};
	return left + right;
}
