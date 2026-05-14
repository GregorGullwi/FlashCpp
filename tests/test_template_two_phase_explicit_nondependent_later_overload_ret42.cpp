template <class U>
int choose(long) {
	return 42;
}

template <class T>
int call_explicit_nondependent() {
	return choose<T>(0);
}

template <class U>
int choose(int) {
	return 7;
}

int main() {
	return call_explicit_nondependent<int>();
}
