typedef struct {
	short grid[2][2];
	double weights[2];
	int (*handlers[2])(int);
} InlineRecord;

typedef union {
	int values[4];
	long long wide[2];
} InlineUnion;

int add_one(int value) {
	return value + 1;
}

int times_two(int value) {
	return value * 2;
}

int main() {
	InlineRecord record{{{1, 2}, {3, 4}}, {1.5, 2.5}, {add_one, times_two}};
	if (record.grid[0][1] != 2 || record.grid[1][0] != 3) return 1;
	if (record.weights[0] != 1.5 || record.weights[1] != 2.5) return 2;
	if (record.handlers[0](5) != 6 || record.handlers[1](5) != 10) return 3;
	if (sizeof(InlineRecord) != 40) return 4;
	InlineUnion source{{5, 6, 7, 8}};
	InlineUnion copy(source);
	if (copy.values[3] != 8 || sizeof(InlineUnion) != 16) return 5;
	return 0;
}
