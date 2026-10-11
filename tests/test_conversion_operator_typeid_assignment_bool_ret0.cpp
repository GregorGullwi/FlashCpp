template<int Bias>
struct NumberSource {
	int value;
	operator int() { return value + Bias; }
	operator int() const { return value + Bias * 2; }
};

struct BoolSource {
	int value;
	operator bool() { return value == 1; }
	operator bool() const { return value == 2; }
};

int global_result = 0;

int main() {
	NumberSource<3> mutable_source{1};
	const NumberSource<3> const_source{2};
	int local_result = 0;
	long long widened_result = 0;
	local_result = mutable_source;
	global_result = mutable_source;
	widened_result = const_source;
	if (local_result != 4 || global_result != 4 || widened_result != 8)
		return 1;

	BoolSource mutable_flag{1};
	const BoolSource const_false{1};
	const BoolSource const_true{2};
	if (!mutable_flag)
		return 2;
	if (const_false)
		return 3;
	if (!const_true)
		return 4;
	if (!(mutable_flag || const_false))
		return 5;
	if (mutable_flag && const_false)
		return 6;
	const int selected = mutable_flag ? 5 : 7;
	if (selected != 5)
		return 7;
	return 0;
}
