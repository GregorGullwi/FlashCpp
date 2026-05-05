struct Target {
	int value;

	explicit Target(int x) : value(x + 1000) {}
	Target(double d, int bias = 2) : value(static_cast<int>(d) + bias) {}
};

struct Source {
	int value;

	Source(int x) : value(x) {}
};

struct StructTarget {
	int value;

	StructTarget(Source s, int bias = 2) : value(s.value + bias) {}
};

int consume(Target t) {
	return t.value;
}

int consumeStruct(StructTarget t) {
	return t.value;
}

struct Sink {
	int consume(Target t) {
		return t.value;
	}

	int consumeStruct(StructTarget t) {
		return t.value;
	}
};

StructTarget makeFromIdent(Source source) {
	return source;
}

StructTarget makeFromTemp() {
	return Source(38);
}

int main() {
	int source = 40;
	if (consume(source) != 42)
		return 1;

	Source struct_source = 40;
	Source other_source = 38;
	if (consumeStruct(struct_source) + consumeStruct(other_source) != 82)
		return 2;

	Sink sink;
	if (sink.consume(source) + sink.consume(38) != 82)
		return 3;
	if (sink.consumeStruct(struct_source) + sink.consumeStruct(Source(38)) != 82)
		return 4;

	StructTarget returned_ident = makeFromIdent(struct_source);
	StructTarget returned_temp = makeFromTemp();
	if (returned_ident.value + returned_temp.value != 82)
		return 5;

	Target from_ident = source;
	Target from_literal = 38;
	if (from_ident.value + from_literal.value != 82)
		return 6;

	StructTarget struct_from_ident = struct_source;
	StructTarget struct_from_temp = Source(38);
	if (struct_from_ident.value + struct_from_temp.value != 82)
		return 7;

	return 42;
}
