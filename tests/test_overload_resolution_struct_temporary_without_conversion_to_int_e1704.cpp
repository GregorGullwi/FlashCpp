int pick(int) {
	return 0;
}

struct NotInt {};

int main() {
	return pick(NotInt{});
}
