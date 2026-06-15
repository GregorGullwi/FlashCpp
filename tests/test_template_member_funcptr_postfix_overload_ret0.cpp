struct Box {
	int value;
};

Box makeBox() {
	return Box{42};
}

int pick(Box) {
	return 0;
}

int pick(int) {
	return 1;
}

template <class T>
struct Holder {
	Box (*callback)();

	int run() {
		return pick(this->callback());
	}
};

int main() {
	Holder<int> holder{makeBox};
	return holder.run();
}
