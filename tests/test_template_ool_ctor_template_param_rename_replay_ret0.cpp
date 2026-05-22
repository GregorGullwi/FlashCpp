template<class T>
struct Box {
	int value;

	template<class U>
	Box(U v);
};

template<class T>
template<class V>
Box<T>::Box(V v) : value(v) {}

int main() {
	Box<int> box(42);
	return box.value == 42 ? 0 : 1;
}
