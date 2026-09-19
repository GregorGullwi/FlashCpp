int main() {
	int values[3] = {4, 5, 6};
	decltype(static_cast<int(*)[3]>(&values)) p = &values;
	return sizeof(p) == sizeof(void*) && (*p)[1] == 5 ? 42 : 0;
}
