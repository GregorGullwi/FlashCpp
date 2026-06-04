template <typename T>
struct OverloadClone {
	constexpr int pick(int) const {
		return 10;
	}

	constexpr int pick(long) const {
		return 20;
	}

	constexpr int eval() const {
		return pick(T{});
	}
};

static_assert(OverloadClone<int>{}.eval() == 10);
static_assert(OverloadClone<long>{}.eval() == 20);

int main() {
	if (OverloadClone<int>{}.eval() != 10) {
		return 1;
	}
	if (OverloadClone<long>{}.eval() != 20) {
		return 2;
	}
	return 0;
}
