using Callback = int (*)(int);

template <class Function>
struct Holder {
	Function callback;
};

int increment(int value) {
	return value + 1;
}

int decrement(int value) {
	return value - 1;
}

int classify(const Callback&) {
	return 10;
}

int classify(const Callback&&) {
	return 20;
}

int selectCallback(bool condition) {
	Callback mutable_callback = &increment;
	const Callback constant_callback = &decrement;
	volatile Callback volatile_callback = &increment;
	if (classify(condition ? constant_callback : volatile_callback) != 20) {
		return 8;
	}
	if (classify(condition ? mutable_callback : constant_callback) != 10) {
		return 4;
	}
	if (classify(condition ? constant_callback : mutable_callback) != 10) {
		return 5;
	}
	const Callback& selected = condition ? mutable_callback : constant_callback;
	if (&selected != (condition ? &mutable_callback : &constant_callback)) {
		return 1;
	}
	Holder<Callback> holder{&increment};
	const Holder<Callback> constant_holder{&decrement};
	if (classify(condition ? holder.callback : constant_holder.callback) != 10) {
		return 6;
	}
	if (classify(condition ? constant_holder.callback : holder.callback) != 10) {
		return 7;
	}
	const Callback& member = condition ? holder.callback : constant_holder.callback;
	if (&member != (condition ? &holder.callback : &constant_holder.callback)) {
		return 2;
	}
	return selected(20) == (condition ? 21 : 19) && member(30) == (condition ? 31 : 29) ? 0 : 3;
}

int main() {
	if (selectCallback(true) != 0) {
		return 1;
	}
	return selectCallback(false) == 0 ? 0 : 2;
}
