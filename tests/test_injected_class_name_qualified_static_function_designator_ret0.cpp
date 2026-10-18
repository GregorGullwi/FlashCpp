struct Source {
	static int answer() {
		return 42;
	}
};

using SourceAlias = Source;

struct Receiver {
	explicit Receiver(int (*function)()) : value(function()) {}
	int value;
};

int main() {
	Receiver injected_class_name(Source::Source::answer);
	Receiver direct_class_name(Source::answer);
	Receiver typedef_alias(SourceAlias::answer);
	return injected_class_name.value == 42 && direct_class_name.value == 42 && typedef_alias.value == 42 ? 0 : 1;
}