constexpr int compute() {
	int x = 1;
	return (x = 2, x + 40);
}

constexpr int advanceTwice() {
	int x = 5;
	return ((x += 3), (x *= 4), x);
}

static_assert(compute() == 42);
static_assert(advanceTwice() == 32);

int main() {
	return (compute() == 42 && advanceTwice() == 32)
		? 0
		: 1;
}
