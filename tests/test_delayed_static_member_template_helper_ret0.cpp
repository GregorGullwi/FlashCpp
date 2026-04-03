template <typename T>
struct Box {
	template <typename U>
	static int helper() { return int(sizeof(T)) + int(sizeof(U)) * 10 + 30; }

	static int value() { return helper<long long>(); }
};

int main() {
	return Box<char>::value() == 111 ? 0 : 1;
}
