// Same-spelling member templates have distinct published TemplateDeclIds under
// their class-owned OwnerIds. Their legacy type-map instance keys must keep
// that owner identity, while repeated uses of each member still reuse its key.
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

int main() {
	OuterA::Inner::Box<short> first{7};
	OuterB::Inner::Box<short> second{11};
	OuterA::Inner::Box<short> first_again{5};
	return static_cast<int>(first.first) + static_cast<int>(second.second) +
		static_cast<int>(first_again.first) - 23;
}
