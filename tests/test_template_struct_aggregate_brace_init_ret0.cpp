template<typename T>
struct Pair {
	T first;
	T second;
};

struct Leaf {
	int value;
};

struct TemplateAggregate {
	Pair<Leaf> nested;
	int tail;
};

int main() {
	TemplateAggregate v = {{{10}, {20}}, 42};
	return (v.nested.first.value == 10 &&
		v.nested.second.value == 20 &&
		v.tail == 42) ? 0 : 1;
}
