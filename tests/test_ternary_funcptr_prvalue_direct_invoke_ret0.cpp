int plus1(int x) {
	return x + 1;
}

using Fn = int (*)(int);

Fn get() {
	return plus1;
}

Fn forward(Fn fn) {
	return fn;
}

int main() {
	return (true ? forward(get()) : get())(41) == 42 ? 0 : 1;
}
