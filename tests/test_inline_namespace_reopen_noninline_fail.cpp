namespace outer {
	int value = 0;
}

namespace outer::inner {
	int first() { return 1; }
}

namespace outer::inline inner {
	int second() { return 2; }
}
