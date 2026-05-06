int FOO(int x) { return x + 5; }

#define FOO(a, b) ((a) + (b))

int main() {
	int prefix = 2;
	return prefix + FOO(3) == 10 ? 0 : 1;
}
