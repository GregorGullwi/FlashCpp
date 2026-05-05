template<typename T>
struct DerefBase {
	T* ptr;

	T& operator*() {
		return *ptr;
	}

	const T& operator*() const {
		return *ptr;
	}
};

template<typename T>
struct Iter : DerefBase<T> {};

int main() {
	int value = 42;
	Iter<int> it;
	it.ptr = &value;
	const Iter<int>& const_it = it;

	int sum = *it;
	sum += *const_it;
	return sum - 84;
}
