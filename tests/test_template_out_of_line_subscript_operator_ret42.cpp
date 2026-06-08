template<typename T>
struct Box {
	T values[2];

	T& operator[](int index);
};

template<typename T>
T& Box<T>::operator[](int index) {
	return values[index];
}

int main() {
	Box<int> box{{10, 42}};
	return box[1];
}
