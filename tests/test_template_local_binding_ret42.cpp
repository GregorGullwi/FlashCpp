struct Box {
	int value;

	Box(int x) : value(x) {}
};

template <int N>
int f() {
	Box box(N);
	return box.value;
}

int main() {
	return f<42>();
}
