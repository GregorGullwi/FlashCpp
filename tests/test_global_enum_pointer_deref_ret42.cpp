enum Value {
	First = 10,
	Second = 20,
	Answer = 42
};

Value values[3] = {First, Second, Answer};
Value* g_value_ptr = &values[0];

int main() {
	Value* p = g_value_ptr + 2;
	return p ? static_cast<int>(*p) : 0;
}
