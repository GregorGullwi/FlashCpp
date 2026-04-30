int plus1(int x) {
	return x + 1;
}

int plus2(int x) {
	return x + 2;
}

using Fn = int (*)(int);

Fn choose(bool pick_first) {
	return pick_first ? plus1 : plus2;
}

Fn forward(Fn fn) {
	return fn;
}

int main() {
	int nested_direct = forward(choose(true))(41);
	int ternary_direct = false ? forward(choose(false))(40) : forward(choose(true))(41);
	return (nested_direct == 42 && ternary_direct == 42) ? 0 : 1;
}
