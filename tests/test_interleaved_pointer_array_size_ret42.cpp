using I = int;

int main() {
	int (*(*p)[3])[4] = nullptr;
	I (*(*alias_p)[3])[4] = nullptr;
	return sizeof(*p) == 3 * sizeof(int (*)[4]) &&
			sizeof(*alias_p) == sizeof(*p)
		? 42
		: 1;
}
