using Row = short[3];

struct Ambiguous {
	Row row;
	int values[3];

	operator Row&() { return row; }
	operator int*() { return values; }
};

int main() {
	Ambiguous value;
	return value[0];
}
