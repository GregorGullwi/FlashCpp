struct FlatMemberArrayItem {
	int value;
};

struct FlatMemberArrayHolder {
	FlatMemberArrayItem items[2];
	int tail;
};

constexpr int flat_member_array_result() {
	FlatMemberArrayHolder holder = {40, 2, 7};
	return holder.items[0].value + holder.items[1].value + holder.tail;
}

static_assert(flat_member_array_result() == 49);

int main() {
	return flat_member_array_result() == 49 ? 0 : 1;
}
