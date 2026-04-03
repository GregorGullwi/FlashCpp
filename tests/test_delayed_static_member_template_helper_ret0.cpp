template <typename T>
struct Box {
	template <typename U>
	static int helper() { return int(sizeof(T) + sizeof(U)) + 40; }

	static int value() { return helper<int>(); }
};

int main() {
	return Box<int>::value() == 48 ? 0 : 1;
}
