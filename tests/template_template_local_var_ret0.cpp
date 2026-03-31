// Test: local variable of type Container<T> inside template template function
template <typename T>
struct MyVec {
	T data;
};

template <template <typename> class Container, typename T>
int use_container() {
	Container<T> v;
	v.data = 42;
	return v.data;
}

int main() {
	return use_container<MyVec, int>() - 42;
}
