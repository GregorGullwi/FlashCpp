using Callback = int (*)(int);

struct Holder {
	Callback callback;
};

int increment(int value) {
	return value + 1;
}

int decrement(int value) {
	return value - 1;
}

int selectCallback(bool condition) {
	Holder holder{&increment};
	Callback local = &decrement;
	Callback first = condition ? holder.callback : local;
	Callback second = condition ? local : holder.callback;
	return first(20) * 100 + second(20);
}

int assignCallback(bool condition) {
	Holder holder{&increment};
	Callback local = &decrement;
	Callback& selected = condition ? holder.callback : local;
	selected = &increment;
	(condition ? holder.callback : local) = &decrement;
	return holder.callback(20) * 100 + local(20);
}

int main() {
	if (selectCallback(true) != 2119) {
		return 1;
	}
	if (selectCallback(false) != 1921) {
		return 2;
	}
	if (assignCallback(true) != 1919) {
		return 3;
	}
	return assignCallback(false) == 2119 ? 0 : 4;
}
