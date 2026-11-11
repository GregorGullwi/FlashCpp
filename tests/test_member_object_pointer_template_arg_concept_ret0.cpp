struct Host {
	int field;
};

template <typename Type>
concept MemberObjectPointerish = __is_member_object_pointer(Type);

template <typename Type>
concept NotMemberObjectPointerish = !__is_member_object_pointer(Type);

static_assert(MemberObjectPointerish<int Host::*>);
static_assert(MemberObjectPointerish<long Host::*>);
static_assert(!MemberObjectPointerish<int*>);
static_assert(!MemberObjectPointerish<int>);
static_assert(NotMemberObjectPointerish<int>);

int main() {
	return 0;
}
