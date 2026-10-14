// A valid out-of-line static member function of a class template must still
// attach and compile. It may reference another static data member directly,
// and the structured-rejection propagation for replayed bodies must not fire
// for a well-formed body.

template <typename T>
struct StaticBox {
	static int value;
	static int get();
};

template <typename T>
int StaticBox<T>::value = 42;

template <typename T>
int StaticBox<T>::get() {
	return value;
}

int main() {
	return StaticBox<long>::get();
}
