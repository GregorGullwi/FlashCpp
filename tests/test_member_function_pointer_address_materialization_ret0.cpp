// A runtime member-function-pointer address-of must lower to the selected
// member's code symbol. Before this was implemented, the address fell through
// to a generic AddressOf that stubbed a zero placeholder without writing the
// result slot, so a non-null comparison depended on unrelated stack garbage.
template <class Type>
struct Box {
	Type value;
};

struct Small {
	int read(int value) const & {
		return value;
	}
};

struct Large {
	template <class Type>
	int read(Box<Type>*) const & {
		return 1;
	}

	template <class Type>
	int read(Type*) const & {
		return 2;
	}
};

using SmallPointer = int (Small::*)(int) const &;
using LargePointer = int (Large::*)(Box<Box<int>>*) const &;

static bool holdsAddress(SmallPointer pointer) {
	return pointer != nullptr;
}

int main() {
	SmallPointer implicit_small = &Small::read;
	SmallPointer cast_small = static_cast<SmallPointer>(&Small::read);
	LargePointer cast_large = static_cast<LargePointer>(&Large::read);
	SmallPointer null_small = nullptr;

	return implicit_small != nullptr &&
			holdsAddress(cast_small) &&
			cast_large != nullptr &&
			null_small == nullptr
		? 0
		: 1;
}
