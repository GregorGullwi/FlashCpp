template <typename T>
struct Box {
	using value_type = T;

	int set(value_type v);
};

template <typename T>
int Box<T>::set(T v) {
	return static_cast<int>(v);
}

int main() {
	Box<int> box;
	return box.set(42) == 42 ? 0 : 1;
}
