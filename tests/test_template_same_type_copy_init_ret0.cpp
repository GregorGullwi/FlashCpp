template<typename T>
struct Box {
	T value;

	Box(T v)
		: value(v) {}
};

int main() {
	Box<int> a = 7;
	Box<int> b = a;
	return b.value == 7 ? 0 : 1;
}
