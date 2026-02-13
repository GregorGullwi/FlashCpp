template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		unsigned int flag : 1;
		unsigned int flag2 : 1 = 1;
		int x = 5;
	};
};

int main() {
	Outer<int>::Inner<int> value{};
	value.flag = 1;
	if (value.flag2 != 1) {
		return 99;
	}
	return value.x + value.flag;
}
