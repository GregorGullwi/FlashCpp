template <typename T>
struct Box {
	T value;

	Box& operator=(const Box& other) {
		value = other.value;
		return *this;
	}

	int copyFrom(const Box& other) {
		return operator=(other).value;
	}
};

int main() {
	Box<int> src{42};
	Box<int> dst{0};
	return dst.copyFrom(src);
}
