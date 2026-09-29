// Nested function-pointer parameters must keep distinct TypeId identity so
// int (*)(int) and int (**)(int) are separate overloads.
int choose(int (*)(int)) { return 1; }
int choose(int (**)(int)) { return 2; }

int target(int value) { return value; }

int main() {
	int (*single)(int) = &target;
	int (**nested)(int) = &single;
	if (choose(single) != 1)
		return 1;
	if (choose(nested) != 2)
		return 2;
	return 0;
}
