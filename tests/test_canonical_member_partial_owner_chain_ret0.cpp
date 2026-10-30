// Instantiations that select a member class-template partial pattern keep the
// partial's own fields while the enclosing class-template arguments vary.
template<class T> struct Owner {
	template<class U> struct Box { U value; };
	template<class U> struct Box<U*> {
		U* pointer;
		int tag;
	};
};

struct Payload {
	short first;
	int second;
};

using ShortPointer = Owner<short>::Box<int*>;
using WidePointer = Owner<long long>::Box<int*>;
using PayloadPointer = Owner<short>::Box<Payload*>;

static_assert(sizeof(ShortPointer) == sizeof(void*) + sizeof(int) + sizeof(int));
static_assert(sizeof(WidePointer) == sizeof(ShortPointer));
static_assert(sizeof(PayloadPointer) == sizeof(ShortPointer));

int main() {
	ShortPointer narrow{};
	WidePointer wide{};
	PayloadPointer payload{};
	narrow.pointer = nullptr;
	wide.pointer = nullptr;
	payload.pointer = nullptr;
	narrow.tag = 1;
	wide.tag = 2;
	payload.tag = 3;
	return narrow.tag + wide.tag + payload.tag - 6;
}
