// Used free inline definitions must still be emitted and linked.

inline int used_helper(int x) {
	return x + 1;
}

int main() {
	return used_helper(0) == 1 ? 0 : 1;
}
