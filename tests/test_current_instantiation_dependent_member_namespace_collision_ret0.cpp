namespace first {
template<class T>
struct Box {
	static int X;
};
}

namespace selected {
template<class T>
struct Box {
	using X = T;

	static int check() {
		return sizeof(Box<T>::X) == sizeof(T) ? 0 : 1;
	}
};
}

int main() {
	return selected::Box<long long>::check();
}
