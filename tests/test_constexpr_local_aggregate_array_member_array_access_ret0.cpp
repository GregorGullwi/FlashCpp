struct LocalAggregateArrayMemberArrayItem {
	int data[2];
};

constexpr int local_aggregate_array_member_array_access_result() {
	LocalAggregateArrayMemberArrayItem items[] = {{{40, 1}}, {{2, 3}}};
	return items[0].data[0] + items[1].data[0];
}

static_assert(local_aggregate_array_member_array_access_result() == 42);

int main() {
	return 0;
}