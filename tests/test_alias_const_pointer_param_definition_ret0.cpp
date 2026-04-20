using ptr_alias = void*;

int f(ptr_alias p);

int f(ptr_alias const p) {
	(void)p;
	return 0;
}

int main() {
	return f((ptr_alias)0);
}
