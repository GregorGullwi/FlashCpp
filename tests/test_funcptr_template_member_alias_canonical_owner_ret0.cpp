int increment(int value) {
	return value + 1;
}

long long widen(long long value) {
	return value + 2;
}

template <typename Function>
struct CallbackHolder {
	using callback_type = Function;
	callback_type callback;
};

int main() {
	CallbackHolder<int (*)(int)> narrow;
	CallbackHolder<long long (*)(long long)> wide;
	narrow.callback = increment;
	wide.callback = widen;

	return narrow.callback(41) == 42 && wide.callback(40) == 42 ? 0 : 1;
}
