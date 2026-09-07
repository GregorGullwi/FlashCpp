// Regression: brace-initialized arrays of records must keep aggregate-init
// values. Default construction must not re-run over already-initialized
// elements ([dcl.init.aggr], [dcl.init.list]).
struct Sample {
	char small;
	double large;
};

struct Inner {
	char c;
	short s;
};

struct Outer {
	Inner a;
	Inner b;
};

struct Mix {
	unsigned char u8;
	int i32;
	long long i64;
};

int main() {
	Sample values[2] = {{7, 9.5}, {11, 4.5}};
	if (values[0].small != 7)
		return 1;
	if (values[0].large != 9.5)
		return 2;
	if (values[1].small != 11)
		return 3;
	if (values[1].large != 4.5)
		return 4;
	if (values[1].small - 11 != 0)
		return 5;

	Outer nested[2] = {{{1, 2}, {3, 4}}, {{5, 6}, {7, 8}}};
	if (nested[0].a.c != 1)
		return 10;
	if (nested[0].a.s != 2)
		return 11;
	if (nested[0].b.c != 3)
		return 12;
	if (nested[0].b.s != 4)
		return 13;
	if (nested[1].a.c != 5)
		return 14;
	if (nested[1].a.s != 6)
		return 15;
	if (nested[1].b.c != 7)
		return 16;
	if (nested[1].b.s != 8)
		return 17;

	Mix mixed[3] = {
		{1, 2, 3},
		{4, 5, 6},
		{7, 8, 9},
	};
	if (mixed[0].u8 != 1 || mixed[0].i32 != 2 || mixed[0].i64 != 3)
		return 20;
	if (mixed[1].u8 != 4 || mixed[1].i32 != 5 || mixed[1].i64 != 6)
		return 21;
	if (mixed[2].u8 != 7 || mixed[2].i32 != 8 || mixed[2].i64 != 9)
		return 22;

	Outer trailing[2] = {{{9, 8}, {7, 6}}};
	if (trailing[0].a.c != 9 || trailing[0].b.s != 6)
		return 30;
	if (trailing[1].a.c != 0 || trailing[1].a.s != 0)
		return 31;
	if (trailing[1].b.c != 0 || trailing[1].b.s != 0)
		return 32;

	return 0;
}
