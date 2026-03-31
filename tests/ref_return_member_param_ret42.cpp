struct Holder {
	int& pass(int& x) { return x; }
};

int main() {
	Holder h;
	int x = 0;

	h.pass(x) = 40;
	h.pass(x) += 2;

	return x;
}
