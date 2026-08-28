// Diagnostic regression for operator-> arity.
struct ArrowWrapper {
	ArrowWrapper* operator->(int extra) {
		return this;
	}
};

int main() {
	return 0;
}
