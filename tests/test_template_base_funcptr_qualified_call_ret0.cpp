int increment(int value) {
	return value + 1;
}

struct Base {
	int (*callback)(int);
};

template <class T>
struct Holder : Base {
	int run() {
		return this->Base::callback(41) - 42;
	}
};

int main() {
	Holder<int> holder;
	holder.callback = increment;
	return holder.run();
}
