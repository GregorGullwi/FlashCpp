int add3(int a, int b, int tail) {
	return a + b + tail;
}

int tailKind(long) {
	return 12;
}

template <int Prefix, int... Ns, typename U>
int sumValues(U tail) {
	return add3(Ns..., tailKind(tail)) - Prefix;
}

int main() {
	return sumValues<0, 10, 20>(0L) == 42 ? 0 : 1;
}
