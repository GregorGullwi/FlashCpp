template <typename T>
struct Box {
	struct Holder {
		int value;

		constexpr int get() const { return value; }
	};

	static constexpr Holder make_holder() {
		return Holder{static_cast<int>(sizeof(T)) + 38};
	}

	static constexpr int value = make_holder().get();
};

static_assert(Box<short>::value == 40);
static_assert(Box<double>::value == 46);

int main() {
	return Box<int>::value;
}
