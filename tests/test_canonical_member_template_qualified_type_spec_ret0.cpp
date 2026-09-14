// Member class-template type-ids spelled through qualified owner chains
// resolve through published TemplateDeclId identity, not registry
// owner-chain spelling keys. Two same-spelling member templates under
// distinct owners keep distinct identity through parameter-type,
// return-type, and local-variable positions. Mixed native widths and a
// struct argument exercise the path.
struct PayloadA {
	int weight;
};

struct PayloadB {
	long height;
};

struct OuterA {
	struct Inner {
		template<typename T>
		struct Box {
			T first;
		};
	};
};

struct OuterB {
	struct Inner {
		template<typename T>
		struct Box {
			T second;
		};
	};
};

namespace ns {
struct Holder {
	template<typename U>
	struct Slot {
		U value;
	};
};
}

int useBoxA(OuterA::Inner::Box<PayloadA> boxed) {
	return boxed.first.weight;
}

int useBoxB(OuterB::Inner::Box<PayloadB> boxed) {
	return static_cast<int>(boxed.second.height);
}

ns::Holder::Slot<short> makeSlot() {
	ns::Holder::Slot<short> slot{4};
	return slot;
}

int main() {
	OuterA::Inner::Box<short> first{7};
	OuterB::Inner::Box<short> second{11};
	OuterA::Inner::Box<PayloadA> boxed_a{PayloadA{5}};
	OuterB::Inner::Box<PayloadB> boxed_b{PayloadB{3}};
	ns::Holder::Slot<short> slot = makeSlot();
	return static_cast<int>(first.first) + static_cast<int>(second.second) +
		useBoxA(boxed_a) + useBoxB(boxed_b) +
		static_cast<int>(slot.value) - 30;
}
