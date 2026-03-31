struct Pick {
	int which;

	constexpr Pick(int)
		: which(1) {}

	constexpr Pick(long long)
		: which(2) {}
};

constexpr int fromConstexprLocalInt() {
	constexpr int value = 42;
	Pick pick{value};
	return pick.which;
}

constexpr int fromConstexprAutoInt() {
	constexpr auto value = 42;
	Pick pick{value};
	return pick.which;
}

constexpr int fromConstexprParamInt(int value) {
	Pick pick{value};
	return pick.which;
}

constexpr int fromConstexprParamLongLong(long long value) {
	Pick pick{value};
	return pick.which;
}

static_assert(fromConstexprLocalInt() == 1);
static_assert(fromConstexprAutoInt() == 1);
static_assert(fromConstexprParamInt(42) == 1);
static_assert(fromConstexprParamLongLong(42) == 2);

int main() {
	return (fromConstexprLocalInt() == 1 &&
			fromConstexprAutoInt() == 1 &&
			fromConstexprParamInt(42) == 1 &&
			fromConstexprParamLongLong(42) == 2)
			   ? 0
			   : 1;
}