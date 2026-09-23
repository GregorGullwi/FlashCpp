struct S {
	int first;
	int second;
	long long wide;
	int f() { return first; }
};

template<class T>
bool present(T S::*member) {
	return member;
}

int main() {
	int S::* first = &S::first;
	int S::* second = &S::second;
	int S::* null_data = nullptr;
	int S::* value_initialized{};
	long long S::* wide = &S::wide;
	long long S::* null_wide = static_cast<long long S::*>(nullptr);
	int (S::*null_function)() = nullptr;
	bool direct = &S::first;
	bool null_direct = null_data;
	bool function_direct = null_function;
	if (!first) return 1;
	if (!second) return 4;
	if (null_data) return 5;
	if (value_initialized) return 6;
	if (!direct || null_direct) return 7;
	if (!wide || !present(wide) || present(null_wide)) return 8;
	if (null_function || function_direct) return 9;
	if (!static_cast<bool>(first) || static_cast<bool>(null_data)) return 2;
	if (static_cast<bool>(null_function)) return 11;
	if (!(first && second) || (first && null_data)) return 3;
	return 0;
}
