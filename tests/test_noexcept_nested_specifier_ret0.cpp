// Regression: function noexcept(expr) must evaluate the full constant expression,
// not silently default to noexcept(true) for nested forms like noexcept(noexcept(...)).

void may_throw() {
	throw 7;
}

void wrapper() noexcept(noexcept(may_throw())) {
	throw 7;
}

int main() {
	try {
		wrapper();
		return 1;
	} catch (int value) {
		return value == 7 ? 0 : 2;
	}
}