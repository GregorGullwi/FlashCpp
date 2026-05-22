template<class T>
struct Box {
	template<class U>
	int pick(U);

	template<class U>
	int pick(U, U);
};

template<class T>
template<class U>
int Box<T>::pick(U) { return 1; }

template<class T>
template<class U>
int Box<T>::pick(U, U) { return 42; }

int main() {
	Box<int> b;
	return b.pick(0, 0) - 42;
}
