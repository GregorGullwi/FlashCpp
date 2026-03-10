struct LocalAggregateArrayLoopItem {
	int value;
};

constexpr int local_aggregate_array_for_loop_member_sum_result() {
	LocalAggregateArrayLoopItem items[] = {{20}, {22}};
	int sum = 0;
	for (int i = 0; i < 2; ++i) {
		sum += items[i].value;
	}
	return sum;
}

static_assert(local_aggregate_array_for_loop_member_sum_result() == 42);

int main() {
	return 0;
}