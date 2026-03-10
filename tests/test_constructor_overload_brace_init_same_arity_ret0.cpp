struct Pick {
	int which;

	constexpr Pick(int)
		: which(1) {}

	constexpr Pick(double)
		: which(2) {}
};

Pick globalPick{1.5};

constexpr int chooseDoubleConstexpr() {
	Pick value{1.5};
	return value.which;
}

constexpr int chooseIntConstexpr() {
	Pick value{1};
	return value.which;
}

int chooseDoubleRuntime() {
	Pick value{1.5};
	return value.which;
}

int chooseIntRuntime() {
	Pick value{1};
	return value.which;
}

static_assert(chooseDoubleConstexpr() == 2);
static_assert(chooseIntConstexpr() == 1);

int main() {
	return (globalPick.which == 2 && chooseDoubleRuntime() == 2 && chooseIntRuntime() == 1) ? 0 : 1;
}