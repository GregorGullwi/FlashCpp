int f(long) {
	return 42;
}

template <class T>
int call_nondependent() {
	return f(0);
}

int f(int) {
	return 7;
}

int main() {
	return call_nondependent<int>();
}
