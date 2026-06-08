template<typename T>
struct Counter {
	int value;

	Counter operator++(int);
};

template<typename T>
Counter<T> Counter<T>::operator++(int) {
	Counter old = *this;
	value += 1;
	return old;
}

int main() {
	Counter<int> counter{41};
	counter++;
	return counter.value;
}
