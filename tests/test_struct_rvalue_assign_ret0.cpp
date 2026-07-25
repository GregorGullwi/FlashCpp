// C++20 [over.ics.rank]: prvalue RHS prefers operator=(T&&) over operator=(const T&).
struct S {
	int x;
	int y;
};

S make_s(int v) {
	return S{v, 0};
}

int main() {
	S a{};
	a = make_s(42);
	return a.x == 42 ? 0 : 1;
}
