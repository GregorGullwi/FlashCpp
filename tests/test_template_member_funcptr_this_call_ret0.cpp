int increment(int value) {
	return value + 1;
}

template <class T>
struct Holder {
	int (*callback)(int);

	int run() {
		return this->callback(41) - 42;
	}
};

int main() {
	Holder<int> holder{increment};
	return holder.run();
}
