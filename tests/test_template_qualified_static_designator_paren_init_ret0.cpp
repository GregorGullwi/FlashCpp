template <class T>
struct Box {
	static int answer();
};

template <class T>
int Box<T>::answer() {
	return sizeof(T) == sizeof(int) ? 42 : 0;
}

struct Receiver {
	explicit Receiver(int (*function)()) : value(function()) {}
	int value;
};

int main() {
	Receiver receiver(Box<int>::answer);
	return receiver.value == 42 ? 0 : 1;
}
