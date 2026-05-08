struct Box {
	int data[3];
};

constexpr Box makeBox() {
	Box b{{10, 20, 30}};
	return b;
}

static_assert(sizeof(makeBox().data[1]) == sizeof(int));
static_assert(alignof(makeBox().data[1]) == alignof(int));

int main() {
	return 0;
}
