constexpr int doWork() {
	int x = 41;
	(void)x;
	static_cast<void>(x + 1);
	return x + 1;
}

static_assert(doWork() == 42);

int main() {
	return doWork() - 42;
}
