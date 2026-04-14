// Expected to fail: aggregate members without default member initializers remain indeterminate.
struct Pair {
	int first;
	int second = 7;
};

constexpr int readLocalAggregate() {
	Pair value;
	return value.first + value.second;
}

static_assert(readLocalAggregate() == 7);

int main() {
	return 0;
}
