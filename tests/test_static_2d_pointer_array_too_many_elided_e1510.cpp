// A brace-elided flat pointer list is distributed across the row bounds; more
// scalars than the whole array holds is ill-formed and reports the shared
// TooManyInitializers owner.
int a = 1;
int b = 2;
int c = 3;
int d = 4;
int e = 5;

int* cells[2][2] = {&a, &b, &c, &d, &e};

int main() {
	return 0;
}
