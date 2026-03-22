struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct Target {
	int value;

	Target(Source s, int bias = 2) : value(s.value + bias) {}
};

int main() {
	Source source = 40;
	Target from_ident = source;
	Target from_temp = Source(38);
	return from_ident.value + from_temp.value - 40;
}
