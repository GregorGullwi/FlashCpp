// Regression: typedef struct Name { ... } Name must get implicit copy assignment.
typedef struct S {
	int x;
	char y[4];
} S;

int main() {
	S a{};
	S b{};
	a.x = 11;
	b = a;
	return b.x == 11 ? 0 : 1;
}
