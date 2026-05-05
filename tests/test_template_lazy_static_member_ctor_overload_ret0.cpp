template<typename T>
struct Holder {
	struct Pick {
		int which;

		constexpr Pick(int)
			: which(1) {}

		constexpr Pick(double)
			: which(2) {}
	};

	static constexpr auto makeValue() {
		return 1.5;
	}

	static constexpr Pick pick = Pick(makeValue());
};

int main() {
	return Holder<int>::pick.which == 2 ? 0 : 1;
}
