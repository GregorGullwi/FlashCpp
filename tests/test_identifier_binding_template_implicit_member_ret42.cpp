template<typename T>
struct Box {
	T value;

	int get() {
		return value;
	}
};

int main() {
	Box<int> box{42};
	return box.get();
}
