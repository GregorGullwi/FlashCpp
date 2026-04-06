struct S {
	S(int&) {}
};

S f() {
	return 1;
}

int main() {
	return 0;
}
