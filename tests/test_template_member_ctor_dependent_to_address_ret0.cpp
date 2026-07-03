template <class T>
T* local_to_address(T* value) {
	return value;
}

template <class Ptr>
auto local_to_address(const Ptr& value) {
	return local_to_address(value.operator->());
}

template <class T>
concept AnyIterator = true;

template <class Element>
struct SpanLike {
	Element* data;

	template <AnyIterator It>
	SpanLike(It first, int) : data(local_to_address(first)) {
	}
};

int main() {
	int value = 7;
	SpanLike<int> span(&value, 1);
	return *span.data == 7 ? 0 : 1;
}
