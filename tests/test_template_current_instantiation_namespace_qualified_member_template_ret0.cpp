template<typename T>
struct box {
	using self_type = box<T>;
	using value_type = T;

	int run() const {
		typename self_type::value_type value = 42;
		return value - 42;
	}
};

int main() {
	box<int> b;
	return b.run();
}
