template <typename T>
struct Box {
	template <typename U>
	static int helper() {
		return int(sizeof(T) + sizeof(U)) + 40;
	}

	static int value() {
		return helper<int>();
	}
};

int main() {
	if (Box<char>::value() != 45) {
		return 1;
	}
	if (Box<long long>::value() != 52) {
		return 2;
	}
	return 0;
}
