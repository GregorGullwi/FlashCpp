int values[8];

int main() {
	int base = 1;
	int t0 = base + 1;
	int t1 = base + 2;
	int t2 = base + 3;
	int t3 = base + 4;
	int t4 = base + 5;
	int t5 = base + 6;
	int t6 = base + 7;
	int t7 = base + 8;
	int t8 = base + 9;
	int t9 = base + 10;
	int t10 = base + 11;
	int t11 = base + 12;
	int t12 = base + 13;
	int t13 = base + 14;
	int t14 = base + 15;
	int t15 = base + 16;

	values[5] = 100;
	int index = 5;
	int* named = &values[index];
	int* temporary = &values[(t0 - t0) + index];
	const int total = t0 + t1 + t2 + t3 + t4 + t5 + t6 + t7 + t8 + t9 + t10 + t11 + t12 + t13 + t14 + t15;
	return (*named == 100 && *temporary == 100 && total == 152) ? 0 : 1;
}
