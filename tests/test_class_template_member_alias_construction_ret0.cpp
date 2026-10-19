struct Box {
	int value;
};

struct WideBox {
	long long value;
	short tag;
};

template <typename T>
struct Names {
	using Type = T;
};

int main() {
	const int box_value = Names<Box>::Type{41}.value;
	const long long wide_box_value = Names<WideBox>::Type{42, 7}.value;
	return box_value == 41 && wide_box_value == 42 ? 0 : 1;
}
