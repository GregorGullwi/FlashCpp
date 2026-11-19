template<class T>
struct SubscriptAliasPtrBox {
	using Pointer = T*;
	T data[3];

	template<class U = void>
	operator Pointer() {
		return data;
	}
};

int main() {
	SubscriptAliasPtrBox<int> box;
	box.data[1] = 42;
	return box[1] == 42 ? 0 : 1;
}
