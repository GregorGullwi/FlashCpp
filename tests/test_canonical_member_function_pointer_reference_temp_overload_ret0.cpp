struct Packet {
	short tag;
	int value;
};

template <class Type>
struct Base {
	Type run(Type value) const {
		return value;
	}
};

template <class Type>
struct Derived : Base<Type> {};

using IntBaseRun = int (Base<int>::*)(int) const;
using IntDerivedRun = int (Derived<int>::*)(int) const;
using PacketBaseRun = Packet (Base<Packet>::*)(Packet) const;
using PacketDerivedRun = Packet (Derived<Packet>::*)(Packet) const;

struct IntBaseSelection { char marker[3]; };
struct IntDerivedSelection { char marker[5]; };
struct PacketDerivedSelection { char marker[11]; };

IntBaseSelection choose(IntBaseRun const&) {
	return {};
}

IntDerivedSelection choose(IntDerivedRun const&) {
	return {};
}

IntDerivedSelection bindIntMarker(IntDerivedRun const&) {
	return {};
}

PacketDerivedSelection bindPacketMarker(PacketDerivedRun const&) {
	return {};
}

int acceptDerivedInt(IntDerivedRun const& run) {
	(void)run;
	return 13;
}

int acceptDerivedPacket(PacketDerivedRun const& run) {
	(void)run;
	return 17;
}

template <class Type>
auto chooseAfterSubstitution(Type (Base<Type>::*run)(Type) const) {
	return choose(run);
}

template <class Type>
auto bindDerivedAfterSubstitution(Type (Base<Type>::*run)(Type) const) {
	return bindIntMarker(run);
}

template <class Type>
auto bindPacketAfterSubstitution(Type (Base<Type>::*run)(Type) const) {
	return bindPacketMarker(run);
}

IntBaseRun int_run = &Base<int>::run;
PacketBaseRun packet_run = &Base<Packet>::run;

static_assert(sizeof(decltype(chooseAfterSubstitution<int>(int_run))) ==
	sizeof(IntBaseSelection));
static_assert(sizeof(decltype(bindDerivedAfterSubstitution<int>(int_run))) ==
	sizeof(IntDerivedSelection));
static_assert(sizeof(decltype(bindPacketAfterSubstitution<Packet>(packet_run))) ==
	sizeof(PacketDerivedSelection));

int main() {
	return acceptDerivedInt(int_run) + acceptDerivedPacket(packet_run) - 30;
}
