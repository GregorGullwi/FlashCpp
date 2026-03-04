template<int N>
struct Box {
	int get() { return N; }
};

int main() {
	Box<4> box;
	return box.get() - 4;
}
