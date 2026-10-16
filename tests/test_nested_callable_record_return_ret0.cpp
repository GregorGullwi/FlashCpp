struct Result { long long value; int tag; };
Result make_result(int value) { return Result{value + 2, 7}; }
Result (*select_result(int which))(int) { (void)which; return &make_result; }
Result (*(*make_selector(char which))(int))(int) { (void)which; return &select_result; }
struct Small { short value; char tag; };
Small make_small(int value) { return Small{static_cast<short>(value + 2), 3}; }
Small (*select_small(char which))(int) { (void)which; return &make_small; }

int main() {
	Result (*(*selector)(int))(int) = &select_result;
	Result result = selector(0)(40);
	Result deep = make_selector('x')(0)(40);
	Small small = select_small('x')(40);
	return result.value == 42 && result.tag == 7 && deep.value == 42 && deep.tag == 7 && small.value == 42 && small.tag == 3 ? 0 : 1;
}
