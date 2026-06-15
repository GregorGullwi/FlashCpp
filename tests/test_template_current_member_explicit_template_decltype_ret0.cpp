template <typename U>
char pick(U) {
	return 0;
}

template <typename T>
struct Holder {
	template <typename U>
	static long pick(U) {
		return 2;
	}

	using PickType = decltype(pick<int>(0));

	static int run() {
		return sizeof(PickType) == sizeof(long) ? 0 : 1;
	}
};

int main() {
	return Holder<void>::run();
}
