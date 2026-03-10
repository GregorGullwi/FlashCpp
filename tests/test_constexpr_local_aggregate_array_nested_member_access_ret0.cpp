struct LocalAggregateArrayNestedInner {
	int value;
};

struct LocalAggregateArrayNestedOuter {
	LocalAggregateArrayNestedInner inner;
};

constexpr int local_aggregate_array_nested_member_access_result() {
	LocalAggregateArrayNestedOuter items[] = {{{40}}, {{2}}};
	return items[0].inner.value + items[1].inner.value;
}

static_assert(local_aggregate_array_nested_member_access_result() == 42);

int main() {
	return 0;
}