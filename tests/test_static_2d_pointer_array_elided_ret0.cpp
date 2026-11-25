// [dcl.init.aggr] brace elision: a flat pointer list and a mixed braced list
// both distribute their scalar pointer clauses across the canonical row
// extents, so the second row starts three pointers into the array.
int a = 1;
int b = 2;
int c = 3;
int d = 4;
int e = 5;
int f = 6;
int g = 7;
int h = 8;
int i = 9;
int j = 10;
int k = 11;
int l = 12;

int* flat[2][3] = {&a, &b, &c, &d, &e, &f};
int* mixed[2][3] = {&g, &h, &i, {&j, &k, &l}};

int main() {
	int* (*flat_pointer)[2][3] = &flat;
	if ((*flat_pointer)[0][0] != &a || (*flat_pointer)[0][2] != &c) return 1;
	if ((*flat_pointer)[1][0] != &d || (*flat_pointer)[1][2] != &f) return 2;
	if (*(*flat_pointer)[1][0] != 4) return 3;

	int* (*mixed_pointer)[2][3] = &mixed;
	if ((*mixed_pointer)[0][0] != &g || (*mixed_pointer)[0][2] != &i) return 4;
	if ((*mixed_pointer)[1][0] != &j || (*mixed_pointer)[1][2] != &l) return 5;
	if (*(*mixed_pointer)[1][1] != 11) return 6;

	return 0;
}
