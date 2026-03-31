// Regression: constexpr member function calls on template instantiations must
// keep template_param_names aligned with template_args so sizeof(T) resolves.

template <typename T>
struct Box {
	constexpr Box() {}

	constexpr int size() const {
		return sizeof(T);
	}
};

constexpr Box<int> box = Box<int>();
constexpr int value = box.size();
static_assert(value == 4);

int main() {
	return 42;
}