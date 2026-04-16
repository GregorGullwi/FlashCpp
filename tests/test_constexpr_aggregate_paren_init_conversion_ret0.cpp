struct AggregatePair {
	int first;
	int second;
};

constexpr AggregatePair global_pair(3, 7);

static_assert(global_pair.first == 3);
static_assert(global_pair.second == 7);

constexpr int local_sum() {
	AggregatePair local_pair(9, 2);
	return local_pair.first + local_pair.second;
}

static_assert(local_sum() == 11);

int main() {
	return (global_pair.first == 3 &&
			global_pair.second == 7 &&
			local_sum() == 11)
		? 0
		: 1;
}
