namespace math {
	struct Counter {
		int value;

		explicit Counter(int v) : value(v) {}

		Counter operator++(int) {
			Counter before(*this);
			++value;
			return before;
		}

		int operator()(int delta = 0) const {
			return value + delta;
		}
	};
}

int main() {
	math::Counter c(40);
	auto before_auto = c.operator++(0);
	math::Counter before_typed = c.operator++(0);
	auto auto_call = before_auto.operator()(2);
	const int typed_call = before_typed.operator()(1);
	const int current_call = c.operator()(0);
	return (auto_call == 42 && typed_call == 42 && current_call == 42) ? 0 : 1;
}
