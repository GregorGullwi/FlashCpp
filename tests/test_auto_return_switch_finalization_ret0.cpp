auto pick_value(int selector) {
	switch (selector) {
	case 0:
		return 40;
	default:
		return 42;
	}
}

int main() {
	return pick_value(1) == 42 ? 0 : 1;
}
