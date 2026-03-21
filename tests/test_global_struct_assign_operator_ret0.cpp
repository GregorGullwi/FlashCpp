// Regression: global/static struct assignment must not bypass user-defined operator=.

struct Counter {
	int value;

	Counter& operator=(const Counter& other) {
		value = other.value + 1;
		return *this;
	}
};

Counter g_target{0};

int main() {
	Counter source{41};

	g_target = source;
	if (g_target.value != 42) {
		return 1;
	}

	static Counter s_target{0};
	s_target = source;
	if (s_target.value != 42) {
		return 2;
	}

	return 0;
}
