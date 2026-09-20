// Regression: reading an array member of an element of a local array of
// structs. Member accesses used to load the array's bytes as a scalar value,
// so subscripting `a[i].w[j]` reinterpreted those bytes as a pointer and
// crashed. The member access must yield the member's storage address so the
// subscript indexes the array itself.

struct Tagged {
	int tag;
	int weights[2];
};

struct Large {
	int tag;
	int values[4];
};

struct Small {
	int tag;
	char bytes[3];
};

struct Single {
	int tag;
	int only[1];
};

int main() {
	Tagged tagged[2] = {{1, {2, 3}}, {4, {5, 6}}};
	if (tagged[0].tag != 1) return 1;
	if (tagged[0].weights[0] != 2 || tagged[0].weights[1] != 3) return 2;
	if (tagged[1].weights[0] != 5 || tagged[1].weights[1] != 6) return 3;

	int index = 1;
	if (tagged[0].weights[index] != 3) return 4;

	Large large[2] = {{7, {8, 9, 10, 11}}, {12, {13, 14, 15, 16}}};
	if (large[0].values[3] != 11 || large[1].values[0] != 13) return 5;

	Small small[2] = {{17, {18, 19, 20}}, {21, {22, 23, 24}}};
	if (small[0].bytes[0] != 18 || small[0].bytes[2] != 20 || small[1].bytes[1] != 23) return 6;

	Single single[2] = {{25, {26}}, {27, {28}}};
	if (single[0].only[0] != 26 || single[1].only[0] != 28) return 7;

	return 42;
}
