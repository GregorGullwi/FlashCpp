template <typename T>
struct Counter {
	static int value;
};

template <typename T>
int Counter<T>::value = static_cast<int>(sizeof(T)) + 38;

int main() {
	return Counter<int>::value;
}
