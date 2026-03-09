struct LocalObjectMemberAccessExample {
	int value;
};

constexpr int local_object_member_access_result() {
	LocalObjectMemberAccessExample object{40};
	return object.value + object.value;
}

static_assert(local_object_member_access_result() == 80);

int main() {
	return 0;
}