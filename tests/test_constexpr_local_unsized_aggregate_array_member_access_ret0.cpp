struct UnsizedAggregateArrayItem {
	int value;
};

constexpr int local_unsized_aggregate_array_member_access_result() {
	UnsizedAggregateArrayItem items[] = {{40}, {2}};
	return items[0].value + items[1].value;
}

static_assert(local_unsized_aggregate_array_member_access_result() == 42);

int main() {
	return 0;
}