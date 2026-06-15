int increment(int value) {
	return value + 1;
}

template <class T>
struct Holder {
	int (*callback)(int);
};

int main() {
	Holder<int> holder{};
	holder.callback = increment;
	return holder.callback(41) - 42;
}
