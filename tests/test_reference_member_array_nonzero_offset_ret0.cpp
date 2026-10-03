struct Holder {
	int padding[2];
	int (&values)[3];
};

int main() {
	int values[3] = {1, 2, 3};
	Holder holder{{0, 0}, values};
	if (holder.values[1] != 2) return 1;
	holder.values[1] = 5;
	if (values[1] != 5) return 2;
	return 0;
}
