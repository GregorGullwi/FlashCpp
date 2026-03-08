template<typename T>
struct Counter {
	static int count;

	void bump() {
		count = 40;
		++count;
		count += 1;
	}
};

template<typename T>
int Counter<T>::count = 0;

int main() {
	Counter<int> counter;
	counter.bump();
	return Counter<int>::count;
}
