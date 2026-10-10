// Regression: constructor arguments use the sema-selected cv-ranked conversion operator.

struct Source {
	int value;
	Source(int v) : value(v) {}
	operator int() { return value + 10; }
	operator int() const { return value + 20; }
};

struct Target {
	int value;
	Target(int v) : value(v) {}
};

int main() {
	Source mutable_source(1);
	const Source const_source(2);
	Target mutable_target(mutable_source);
	Target const_target(const_source);
	if (mutable_target.value != 11)
		return 1;
	if (const_target.value != 22)
		return 2;
	return 0;
}
