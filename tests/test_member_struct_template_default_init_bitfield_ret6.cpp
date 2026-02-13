template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		unsigned int flag : 1;
		int x = 5;
	};
};

int main() {
	Outer<int>::Inner<int> value{};
	value.flag = 1;
	return value.x + value.flag;
}
