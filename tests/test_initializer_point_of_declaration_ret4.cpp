int main() {
	int x = sizeof(x);  // x is in scope for its own initializer; sizeof(x) == 4
	return x;           // should return 4 on x86-64 where int is 4 bytes
}
