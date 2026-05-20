namespace std {
template <typename T>
class initializer_list {
public:
	const T* first_;
	const T* last_;

	constexpr initializer_list(const T* f, const T* l) noexcept : first_(f), last_(l) {}

	constexpr const T* begin() const noexcept { return first_; }
	constexpr const T* end() const noexcept { return last_; }
};
} // namespace std

struct IterHolder {
	const int* p;
	constexpr int read() const {
		return *p;
	}
};

struct Outer {
	IterHolder holder;
};

constexpr int readNested(std::initializer_list<int> values) {
	Outer a{IterHolder{values.begin()}};
	Outer b{a}; // copy-list-init should preserve nested member bindings
	return b.holder.read();
}

int main() {
	static_assert(readNested({9, 8, 7}) == 9);
	return readNested({9, 8, 7}) == 9 ? 0 : 1;
}
