struct S {
	union {
		char c;
	};

	int a[sizeof(S)];
};

int main() {
	return 0;
}
