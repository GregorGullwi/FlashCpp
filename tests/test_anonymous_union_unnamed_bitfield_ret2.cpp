struct S {
	union {
		int : 0;
		char c;
	};
	char d;
};

int main() {
	// Clang C++20: sizeof(S) == 2 (anonymous union size 1 + trailing char size 1)
	return sizeof(S);
}
