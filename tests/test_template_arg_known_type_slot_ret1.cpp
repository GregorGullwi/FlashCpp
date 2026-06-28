template <class T>
using AddRef = T&;

template <class T>
struct Box {
	static constexpr int value = 0;
};

template <class T>
struct Box<T&> {
	static constexpr int value = 1;
};

template <class T>
int testKnownTypeSlot() {
	return Box<AddRef<T>>::value;
}

int main() {
	return testKnownTypeSlot<int>() == 1 ? 1 : 0;
}
