int pick(int) {
	return 0;
}

template <typename T>
int callPick(T value) {
	return pick(value);
}

struct NotInt {};

int main() {
	NotInt value;
	return callPick(value);
}
