struct Host {
	int field[3];
	int* pointer_field[3];
};

template <typename Type>
concept MemberObjectPointerish = __is_member_object_pointer(Type);

static_assert(MemberObjectPointerish<int (Host::*)[3]>);
static_assert(MemberObjectPointerish<int* (Host::*)[3]>);
static_assert(!MemberObjectPointerish<int (*)[3]>);
static_assert(!MemberObjectPointerish<int[3]>);

template <typename Type>
struct Box {};

Box<int (*)[3]> pointer_to_array;
Box<int (Host::*)[3]> member_pointer_to_array;

int main() {
	return 0;
}
