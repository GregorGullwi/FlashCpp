template<class T>
struct VirtualBase {
	T data[3];
	template<class U = void>
	operator T*() { return data; }
};

template<class T>
struct VirtualLeaf : virtual VirtualBase<T> {};

int main() {
	VirtualLeaf<int> box;
	box.data[1] = 42;
	return box[1] == 42 ? 0 : 1;
}
