// Phase 3B regression: out-of-line member function templates of class templates
// must see both outer template bindings and implicit member lookup.

template <typename T>
struct Box {
	T value;

	template <typename U>
	T add(U extra);
};

template <typename T>
template <typename U>
T Box<T>::add(U extra) {
	return value + static_cast<T>(extra);
}

int main() {
	Box<int> box{40};
	return box.add<int>(2);
}
