// Direct namespace alias defaults preserve reference wrappers and nominal identity.
template<class T = unsigned short&>
using NativeReference = T;

struct Packet {
	int count;
	long bytes;
};

template<class T = Packet&>
using PacketReference = T;

template<class T = const Packet&>
using ConstPacketReference = T;

template<class T = Packet*&>
using PacketPointerReference = T;

template<class T = unsigned int&&>
using NativeRvalueReference = T;

enum class Status : unsigned char {
	Ready = 3
};

template<class T = Status&>
using StatusReference = T;

static_assert(__is_same(NativeReference<>, unsigned short&));
static_assert(__is_same(PacketReference<>, Packet&));
static_assert(__is_same(ConstPacketReference<>, const Packet&));
static_assert(__is_same(PacketPointerReference<>, Packet*&));
static_assert(__is_same(NativeRvalueReference<>, unsigned int&&));
static_assert(__is_same(StatusReference<>, Status&));

unsigned short native_value = 6;
unsigned int native_rvalue = 2;
Packet packet{4, 9};
Packet alternate_packet{7, 5};
Packet* packet_address = &packet;
Status status = Status::Ready;

NativeReference<> native_reference = native_value;
PacketReference<> packet_reference = packet;
ConstPacketReference<> const_packet_reference = packet;
PacketPointerReference<> packet_pointer_reference = packet_address;
StatusReference<> status_reference = status;

int main() {
	NativeRvalueReference<> native_rvalue_reference = static_cast<unsigned int&&>(native_rvalue);
	native_reference = 10;
	packet_reference.count = 20;
	packet_pointer_reference = &alternate_packet;
	status_reference = Status::Ready;
	if (native_reference != 10) {
		return 1;
	}
	if (packet_reference.count != 20) {
		return 2;
	}
	if (static_cast<int>(status_reference) != 3) {
		return 3;
	}
	if (const_packet_reference.bytes != 9) {
		return 4;
	}
	if (native_rvalue_reference != 2) {
		return 5;
	}
	if (packet_pointer_reference->bytes != 5) {
		return 6;
	}
	return 42;
}
