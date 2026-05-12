template <typename T>
struct LazyOverloadBox {
	int select(int) {
		return 1;
	}

	int select(int*) {
		return 2;
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
	int* ptr = nullptr;
	int first = box.select(0);
	int second = box.select(ptr);
	int third = box.pick(1);
	int fourth = box.pick(ptr);
	return (first == 1 && second == 2 && third == 4 && fourth == 8) ? 0 : 1;
}
