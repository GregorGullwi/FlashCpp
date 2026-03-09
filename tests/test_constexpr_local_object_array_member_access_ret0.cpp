struct LocalArrayMemberAccessExample {
	int data[2];
};

constexpr int local_object_array_member_access_result() {
	LocalArrayMemberAccessExample object{{40, 2}};
	return object.data[0] + object.data[1];
}

static_assert(local_object_array_member_access_result() == 42);

int main() {
	return 0;
}