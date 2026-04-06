template <typename T>
struct Box {
	template <typename U>
	using Storage = U*;

	Storage<T> value;

	explicit Box(T* ptr)
		: value(ptr) {
	}

	T get() const {
		return *value;
	}
};

int main() {
	int value = 0;
	Box<int> box(&value);
	return box.get();
}
