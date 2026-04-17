struct EmptyTag {};

template<typename T>
using empty_alias = EmptyTag;

template<typename T>
int take_empty(empty_alias<T>) {
	return 42;
}

int main() {
	EmptyTag tag{};
	return take_empty<int>(tag);
}
