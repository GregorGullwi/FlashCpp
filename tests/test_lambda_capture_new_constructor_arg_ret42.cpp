// Regression: implicit lambda capture discovery must recurse through the
// constructor-argument surface of a new-expression.

struct Box {
	int value;

	Box(int value) : value(value) {}
};

int main() {
	int captured = 41;
	auto make = [=]() {
		Box* box = new Box(captured + 1);
		int result = box->value;
		delete box;
		return result;
	};
	return make();
}
