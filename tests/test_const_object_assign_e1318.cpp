// Assignment through a const lvalue must still be rejected.
int main() {
	const int x = 1;
	x = 2;
	return 0;
}
