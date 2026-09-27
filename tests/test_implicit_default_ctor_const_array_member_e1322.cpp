struct ConstArrayMember {
	const int values[2];
};

int main() {
	ConstArrayMember object;
	return object.values[0];
}
