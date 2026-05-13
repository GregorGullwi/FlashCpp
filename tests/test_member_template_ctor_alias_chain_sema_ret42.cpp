struct Source {
	int value;
	Source(int v) : value(v) {}
};

using SourceAlias1 = Source;
using SourceAlias2 = SourceAlias1;

struct Box {
	int value;

	template<typename T>
	Box(T t) : value(t.value) {}
};

int main() {
	Box box = Box(SourceAlias2(42));
	return box.value;
}
