template <class U>
int pick(U) {
	return 42;
}

template <class T>
int call_nondependent_template() {
	return pick(0);
}

template <class U>
int pick(int) {
	return 7;
}

int main() {
	return call_nondependent_template<int>();
}
