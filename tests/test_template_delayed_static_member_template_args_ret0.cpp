template <typename U>
int helper(U) {
	return static_cast<int>(sizeof(U)) + 40;
}

template <typename T>
struct Box {
	static int value() {
		return helper<int>('a');
	}
};

int main() {
	if (Box<char>::value() != 44) {
		return 1;
	}
	if (Box<long long>::value() != 44) {
		return 2;
	}
	return 0;
}
