struct Empty {
};

struct Holder {
	Empty empty;
	char value;
};

int main() {
	return sizeof(Empty) == 1 && sizeof(Holder) >= 2 ? 0 : 1;
}
