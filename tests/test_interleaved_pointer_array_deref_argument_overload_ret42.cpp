// Sema-owned overload argument typing must preserve the type produced by
// dereferencing an ordered pointer-to-array declarator.
int select_ordered(int (*(*(*param))[3])[4]) {
	return 42;
}

int select_ordered(double (*(*(*param))[3])[4]) {
	return 1;
}

int main() {
	int (*(*(*rows)[2])[3])[4] = nullptr;
	return select_ordered(*rows);
}
