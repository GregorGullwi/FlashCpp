template<typename T>
struct Holder {
	template<unsigned long long N>
	void consume(int (&arr)[N]);
};

template<typename T>
template<unsigned long long N>
void Holder<T>::consume(int (&arr)[N]) {
	(void)arr;
}

int main() {
	int values[3] = {1, 2, 3};
	Holder<int> holder;
	holder.consume(values);
	return 0;
}
