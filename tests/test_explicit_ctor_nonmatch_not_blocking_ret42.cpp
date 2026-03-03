struct Source {
	int value;
};

struct Target {
	int value;

	explicit Target(const Target& other) : value(other.value) {}
	Target(const Source& src) : value(src.value) {}
};

// Second test: primitive arg where explicit ctor exists alongside non-explicit
struct Target2 {
	int value;

	explicit Target2(int x) : value(x) {}
	Target2(double d) : value(static_cast<int>(d)) {}
};

int useTarget(Target t) {
	return t.value;
}

int useTarget2(Target2 t) {
	return t.value;
}

int main() {
	Source s{42};
	int r1 = useTarget(s);
	int r2 = useTarget2(42.0);
	return (r1 == 42 && r2 == 42) ? 42 : 1;
}
