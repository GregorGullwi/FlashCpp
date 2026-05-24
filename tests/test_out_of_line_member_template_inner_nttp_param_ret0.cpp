template<typename T>
struct Holder {
	template<unsigned long long N>
	void consume(int (&arr)[N]);
};

template<typename T>
template<unsigned long long N>
void Holder<T>::consume(int (&arr)[N]) {
	arr[0] = static_cast<int>(N + arr[0]);
}

int main() {
	int values[3] = {1, 2, 3};
	Holder<int> holder;
	holder.consume(values);
	return values[0] == 4 ? 0 : 1;
}
