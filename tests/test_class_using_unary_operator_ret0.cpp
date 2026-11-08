struct LeftMinus {
	int operator-() const { return 42; }
};

struct RightMinus {
	int operator-() const { return 7; }
};

struct MinusBoth : LeftMinus, RightMinus {
	using LeftMinus::operator-;
};

struct LeftPlus {
	int operator+() const { return 11; }
};

struct RightPlus {
	int operator+() const { return 19; }
};

struct PlusBoth : LeftPlus, RightPlus {
	using RightPlus::operator+;
};

struct LeftBitwiseNot {
	int operator~() const { return 23; }
};

struct RightBitwiseNot {
	int operator~() const { return 31; }
};

struct BitwiseNotBoth : LeftBitwiseNot, RightBitwiseNot {
	using LeftBitwiseNot::operator~;
};

template<class T>
struct TemplateMinusBoth : LeftMinus, RightMinus {
	using LeftMinus::operator-;
	T payload;
};

int main() {
	MinusBoth minus;
	if (-minus != 42) {
		return 1;
	}
	PlusBoth plus;
	if (+plus != 19) {
		return 2;
	}
	BitwiseNotBoth bitwise_not;
	if (~bitwise_not != 23) {
		return 3;
	}
	TemplateMinusBoth<int> templated{};
	return -templated == 42 ? 0 : 4;
}
