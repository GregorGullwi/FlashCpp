struct Host {
	int* field;
};

template <typename Type>
concept MemberObjectPointerish = __is_member_object_pointer(Type);

static_assert(MemberObjectPointerish<int* Host::*>);
static_assert(!MemberObjectPointerish<int*>);
static_assert(!MemberObjectPointerish<int>);

int main() {
	return 0;
}
