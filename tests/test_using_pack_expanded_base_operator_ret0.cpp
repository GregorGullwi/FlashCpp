template <typename T>
struct Base {
	int value;
	int operator-() const { return -value; }
};

template <typename... Bases>
struct Combined : Bases... {
	using Bases::operator-...;
	int operator-() && { return -99; }
};

template <typename T>
concept HasUnaryMinus = requires(T value) { -value; };

static_assert(HasUnaryMinus<Combined<Base<short>>>);

int main() {
	Combined<Base<short>> value{};
	value.value = 7;
	return -value == -7 ? 0 : 1;
}
