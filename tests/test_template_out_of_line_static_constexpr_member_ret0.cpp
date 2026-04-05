template<typename T>
struct OutOfLineTemplateClock {
	static constexpr int epoch_diff = 42;
	static int read();
};

template<typename T>
int OutOfLineTemplateClock<T>::read() {
	return epoch_diff;
}

int main() {
	return OutOfLineTemplateClock<int>::read() - 42;
}
