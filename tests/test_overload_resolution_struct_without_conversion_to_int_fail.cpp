int pick(int) {
	return 0;
}

struct NotInt {};

int main() {
	NotInt value;
	return pick(value);
}
