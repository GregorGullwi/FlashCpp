using Callback = int (*)(int);

struct Callbacks {
	Callback selected;
};

int increment(int value) {
	return value + 1;
}

int invoke(Callback callback, int value) {
	return callback(value);
}

Callback select(Callback callback) {
	return callback;
}

int main() {
	Callbacks callbacks{&increment};
	if (invoke(*callbacks.selected, 41) != 42) {
		return 1;
	}
	Callback local = &increment;
	Callback address = &*callbacks.selected;
	if (address(5) != 6) {
		return 2;
	}
	Callback returned = *select(local);
	return returned(19) == 20 ? 0 : 3;
}
