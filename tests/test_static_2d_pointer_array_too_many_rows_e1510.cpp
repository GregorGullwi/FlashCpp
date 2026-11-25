// More row clauses than the outer bound of a two-dimensional pointer array is
// ill-formed and reports the shared TooManyInitializers owner.
int a = 1;
int b = 2;
int c = 3;
int d = 4;
int e = 5;
int f = 6;

int* cells[2][2] = {{&a, &b}, {&c, &d}, {&e, &f}};

int main() {
	return 0;
}
