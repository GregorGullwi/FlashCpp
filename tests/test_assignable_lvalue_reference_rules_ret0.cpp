struct Value {
	Value& operator=(const Value&) noexcept { return *this; }
};

int main() {
	if (__is_assignable(int, int))
		return 1;
	if (!__is_assignable(int&, int))
		return 2;
	if (__is_assignable(const int&, int))
		return 3;
	if (__is_assignable(int&&, int))
		return 4;
	if (!__is_assignable(Value, Value))
		return 5;
	if (!__is_assignable(Value&, const Value&))
		return 6;
	if (__is_assignable(const Value&, Value))
		return 7;
	if (!__is_assignable(Value&&, const Value&))
		return 8;
	if (__is_assignable(const Value&&, Value))
		return 9;
	if (!__is_nothrow_assignable(Value&, const Value&))
		return 10;
	if (__is_nothrow_assignable(const Value&, Value))
		return 11;
	if (!__is_nothrow_assignable(Value&&, const Value&))
		return 12;
	return 0;
}
