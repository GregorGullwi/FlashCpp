struct Empty {
};

struct alignas(8) AlignedEmpty {
};

struct Holder {
	Empty empty;
	char value;
};

int main() {
	return sizeof(Empty) == 1 &&
		   sizeof(AlignedEmpty) == 8 &&
		   alignof(AlignedEmpty) == 8 &&
		   sizeof(Holder) >= 2 ? 0 : 1;
}
