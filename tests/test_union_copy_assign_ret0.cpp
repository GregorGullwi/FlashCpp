union U {
	int x;
	char y[4];
};

int main() {
	U a{};
	U b{};
	a = b;
	return a.x == b.x ? 0 : 1;
}
