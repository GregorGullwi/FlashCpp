template<typename T>
struct RemoveReference {
	using type = T;
};

template<typename T>
struct RemoveReference<T&> {
	using type = T;
};

template<typename T>
using RemoveReferenceT = typename RemoveReference<T>::type;

template<typename T>
using ConstRef = const RemoveReferenceT<T>&;

int readValue(ConstRef<int&> value) {
	return value;
}

int main() {
	int n = 0;
	return readValue(n);
}
