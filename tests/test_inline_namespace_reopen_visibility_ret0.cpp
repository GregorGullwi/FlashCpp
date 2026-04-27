namespace outer {
	inline namespace v1 {
		struct FromFirstBlock {
			int value;
		};

		int first_value() { return 10; }
	}
}

namespace outer {
	namespace v1 {
		struct FromSecondBlock {
			int value;
		};

		int second_value() { return 20; }
	}
}

int main() {
	outer::FromFirstBlock first{3};
	if (first.value != 3) return 1;

	outer::FromSecondBlock second{4};
	if (second.value != 4) return 2;

	if (outer::first_value() != 10) return 3;
	if (outer::second_value() != 20) return 4;

	return 0;
}
