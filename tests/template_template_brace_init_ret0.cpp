// Test: braced initializer list for Container<T> inside template template function
template <typename T>
struct MyVec {
	T data;
};

template <template <typename> class Container, typename T>
int use_brace_init(T val) {
	Container<T> v{val};
	return v.data;
}

int main() {
	return use_brace_init<MyVec, int>(42) - 42;
}
