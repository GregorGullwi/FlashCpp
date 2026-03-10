template<typename T>
struct Box {
	T value;
	auto get() const;
};

template<typename T>
auto Box<T>::get() const { return value; }

int main() {
	Box<int> box{42};
	return box.get();
}