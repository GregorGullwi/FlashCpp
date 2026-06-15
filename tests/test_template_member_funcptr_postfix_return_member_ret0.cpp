struct Box {
	int value;
};

Box makeBox() {
	return Box{42};
}

template <class T>
struct Holder {
	Box (*callback)();

	int run() {
		return this->callback().value - 42;
	}
};

int main() {
	Holder<int> holder{makeBox};
	return holder.run();
}
