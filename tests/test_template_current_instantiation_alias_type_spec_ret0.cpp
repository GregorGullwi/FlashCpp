template <typename T>
struct Box {
	using self_type = Box<T>;
	using value_type = T;
	using alias = typename self_type::value_type;
	using pointer = alias*;

	static pointer make() {
		return nullptr;
	}
};

int main() {
	Box<int>::pointer value = Box<int>::make();
	return value == nullptr ? 0 : 1;
}
