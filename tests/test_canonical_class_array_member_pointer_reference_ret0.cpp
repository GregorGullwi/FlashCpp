// Class recovery must keep array bounds, and a member-function ref-qualifier is
// not a reference to the pointer. A reference to that pointer is a reference.
struct Packet {
	short tag;
	int value;
};

template <class Type>
struct Box {
	Type value;
};

using PacketRow = Packet[4];
using ConstPacketRow = const Packet[4];
using BoxRow = Box<int>[2];
using QualifiedMember = int (Packet::*)(int) const &;
using MemberReference = QualifiedMember&;

static_assert(__is_array(PacketRow));
static_assert(__is_bounded_array(PacketRow));
static_assert(!__is_class(PacketRow));
static_assert(!__is_same(PacketRow, Packet));
static_assert(__is_same(PacketRow, Packet[4]));
static_assert(__is_const(ConstPacketRow));
static_assert(__is_array(ConstPacketRow));
static_assert(!__is_class(ConstPacketRow));
static_assert(!__is_same(ConstPacketRow, const Packet));
static_assert(__is_array(BoxRow));
static_assert(__is_bounded_array(BoxRow));
static_assert(!__is_class(BoxRow));
static_assert(!__is_same(BoxRow, Box<int>));
static_assert(__is_same(BoxRow, Box<int>[2]));
static_assert(__is_member_function_pointer(QualifiedMember));
static_assert(!__is_lvalue_reference(QualifiedMember));
static_assert(__is_lvalue_reference(MemberReference));
static_assert(!__is_member_function_pointer(MemberReference));

int main() {
	if (!__is_array(PacketRow) || __is_class(PacketRow) || __is_same(PacketRow, Packet)) {
		return 1;
	}
	if (!__is_const(ConstPacketRow) || !__is_array(ConstPacketRow) || __is_same(ConstPacketRow, const Packet)) {
		return 2;
	}
	if (!__is_array(BoxRow) || __is_class(BoxRow) || __is_same(BoxRow, Box<int>)) {
		return 3;
	}
	if (!__is_member_function_pointer(QualifiedMember) || __is_lvalue_reference(QualifiedMember)) {
		return 4;
	}
	if (!__is_lvalue_reference(MemberReference) || __is_member_function_pointer(MemberReference)) {
		return 5;
	}
	return 0;
}
