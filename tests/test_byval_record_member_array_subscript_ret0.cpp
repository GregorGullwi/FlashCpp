struct ByValueMemberArray {
	int seed;
	int values[4];
};

int sum_by_value(ByValueMemberArray value) {
	return value.seed + value.values[0] + value.values[1] + value.values[2] + value.values[3];
}

int main() {
	ByValueMemberArray value = {5, {1, 2, 3, 4}};
	return sum_by_value(value) == 15 ? 0 : 1;
}
