template<typename T>
struct Box {
	T value;

	template<typename U>
	const T& get() {
		return value;
	}
};

int main() {
	Box<int> box{42};
	box.get<int>() = 7;
	return 0;
}
