// Non-template explicit-operator() member call through a const receiver. This
// covers the member-operator branch of the parser, which is separate from the
// named-member branch and from the implicit call-operator path.

struct MutableOnlyCallable {
	int operator()(int value) {
		return value + 1;
	}
};

int useConst(const MutableOnlyCallable& callable) {
	return callable.operator()(41);
}

int main() {
	MutableOnlyCallable callable;
	return useConst(callable);
}
