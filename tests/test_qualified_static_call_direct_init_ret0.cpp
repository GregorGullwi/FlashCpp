struct Arg {
	int value;
	Arg(int v) : value(v) {}
};

struct Helper {
	static Arg make(int v) { return Arg(v); }
};

int main() {
	Arg arg(Helper::make(7));
	return arg.value == 7 ? 0 : 1;
}
