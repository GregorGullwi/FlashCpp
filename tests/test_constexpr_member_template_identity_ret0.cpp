template<typename T>
struct Box {
	constexpr Box() {}

	template <int N>
	constexpr int value() const {
		return static_cast<int>(sizeof(T)) + N;
	}
};

constexpr Box<char> box{};
static_assert(box.value<5>() == 6);

int main() {
	return box.value<5>() == 6 ? 0 : 1;
}
