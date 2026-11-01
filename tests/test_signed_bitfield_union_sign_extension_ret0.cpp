template<class T>
struct UnionBits {
	union {
		T first : 3;
		T second : 3;
	};
};
struct DefaultBits {
	bool enabled : 1 = false;
	int value;
};
struct DirectBits {
	short first : 3;
	short second : 3;
	unsigned short positive : 3;
	bool flag : 1;
	int wide : 5;
	unsigned int uwide : 5;
	long long long_field : 5;
};
int main() {
	UnionBits<short> local{ .second = -1 };
	if (local.second != -1) return 1;
	UnionBits<short>* pointer = &local;
	pointer->first = -2;
	if (pointer->first != -2) return 2;
	DirectBits direct{};
	direct.second = -1;
	if (direct.second != -1) return 3;
	direct.positive = -1;
	if (direct.positive != 7) return 4;
	direct.flag = true;
	if (!direct.flag) return 5;
	direct.wide = -1;
	if (direct.wide != -1) return 6;
	direct.uwide = -1;
	if (direct.uwide != 31) return 7;
	direct.long_field = -1;
	if (direct.long_field != -1) return 8;
	DefaultBits defaults{};
	if (defaults.enabled) return 9;
	defaults.enabled = true;
	if (!defaults.enabled) return 10;
	return 0;
}
