template<int N>
struct Box {
	int get() { return N; }
};

struct Converter {
	template<int N>
	auto value() -> int { return N; }
};

int main() {
	Box<4> box;
	Converter c;
	return box.get() + c.value<6>() - 10;
}
