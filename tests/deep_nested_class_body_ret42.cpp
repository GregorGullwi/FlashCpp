// C++20 [temp.inst]: a class nested two or more levels inside an instantiated
// class template publishes its own body, so naming `Owner<int>::Level1::Level2`
// and using its data members, nested types, and non-static member functions
// works. Cover a three-level body and a namespace-qualified owner, mixing
// native widths with a record member.
struct Payload {
	short tag;
};

template <class T>
struct Outer {
	struct Level1 {
		struct Level2 {
			T value;
			Payload payload;
			T twice() { return value + value; }
			struct Level3 {
				long deep;
			};
		};
	};
};

namespace ns_scope {
template <class T>
struct Outer {
	struct Level1 {
		struct Level2 {
			T value;
		};
	};
};
}  // namespace ns_scope

Outer<int>::Level1::Level2 two_level{5, Payload{4}};
Outer<int>::Level1::Level2::Level3 three_level{20};
ns_scope::Outer<long>::Level1::Level2 ns_two_level{8L};

int main() {
	const int total = two_level.twice() + two_level.payload.tag +
		static_cast<int>(three_level.deep) +
		static_cast<int>(ns_two_level.value);
	return total == 42 ? 42 : total;
}
