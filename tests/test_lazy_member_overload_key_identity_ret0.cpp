template <typename T>
struct LazyOverloadBox {
	int select(int) {
		return 1;
	}

	template <typename U>
	int pick(U) {
		return 4;
	}

	template <typename U>
	int pick(U*) {
		return 8;
	}
};

int main() {
	LazyOverloadBox<int> box;
	int first = box.select(0);
	int second = box.pick(1);
	if (first != 1) {
		return 1;
	}
	if (second != 4) {
		return 2;
	}
	return 0;
}
