template<typename T>
struct Outer {
	template<typename U>
	struct Inner {
		int x = 5;
	};
};

int main() {
	Outer<int>::Inner<int> value{};
	return value.x;
}
