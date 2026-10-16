int calls = 0;
int add_two(int value) { return value + 2; }
int (*select_adder(int which))(int) { (void)which; return &add_two; }
int (*(*make_selector(int which))(int))(int) { (void)which; ++calls; return &select_adder; }

int main() {
	const bool pointers = sizeof(make_selector(0)) == sizeof(void*) && sizeof(make_selector(0)(1)) == sizeof(void*);
	return pointers && sizeof(make_selector(0)(1)(2)) == sizeof(int) && calls == 0 ? 0 : 1;
}
