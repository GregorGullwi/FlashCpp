template<typename T>
struct Box {
	T value{};

	T get() const {
		return value;
	}

	T sum() const {
		return Box<T>::get();
	}
};

int main() {
	Box<int> box;
	box.value = 42;
	return box.sum() - 42;
}
