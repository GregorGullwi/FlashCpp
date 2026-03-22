struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct Target {
	int value;

	Target(Source s, int bias = 2) : value(s.value + bias) {}
};

Target make_from_ident(Source source) {
	return source;
}

Target make_from_temp() {
	return Source(38);
}

int main() {
	Source source = 40;
	Target a = make_from_ident(source);
	Target b = make_from_temp();
	return a.value + b.value - 40;
}
