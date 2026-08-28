struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct Target {
	int value;

	explicit Target(Source s) : value(s.value) {}
};

int main() {
	Source source = 42;
	Target target = source;
	return target.value;
}
