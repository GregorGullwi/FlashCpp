template <typename T>
struct Provider {
	struct Node {
		template <typename U>
		using Apply = T;
	};
};

template <typename T>
struct Outer {
	template <typename U>
	int pick(typename Provider<T>::Node::template Apply<U> value);

	template <typename U>
	int pick(typename Provider<T>::Node::template Apply<U>* value);
};

template <typename T>
template <typename U>
int Outer<T>::pick(typename Provider<T>::Node::template Apply<U> value) {
	return value == 7 ? 0 : 11;
}

template <typename T>
template <typename U>
int Outer<T>::pick(typename Provider<T>::Node::template Apply<U>* value) {
	return value != nullptr && *value == 9 ? 0 : 13;
}

int main() {
	Outer<int> outer;
	int nine = 9;
	if (outer.pick<double>(7) != 0) {
		return 1;
	}
	if (outer.pick<char>(&nine) != 0) {
		return 2;
	}
	return 0;
}
