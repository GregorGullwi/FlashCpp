// A function definition whose return type is a function pointer, or a nested
// callable, keeps the entity's parameter list on the function and the returned
// callable on the returned pointer, so the definition parses and lowers as a
// function rather than being rejected with "Expected primary expression".
int add_two(int x) { return x + 2; }
int (*select_adder(int which))(int) { (void)which; return &add_two; }
int (* (*make_selector(int which))(int))(int) { (void)which; return &select_adder; }

int main() { return 0; }
