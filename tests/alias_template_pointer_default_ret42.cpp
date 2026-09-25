// Direct namespace alias defaults preserve pointer wrappers and nominal identity.
template<class T = int*>
using NativePointer = T;

struct Payload {
	long value;
	short tag;
};

namespace left {
struct Item {
	long value;
};
}

namespace right {
struct Item {
	long value;
};
}

namespace alias_defaults {
struct Namespaced {
	short value;
};

template<class T = Namespaced*>
using NamespacedPointer = T;
}

template<class T = Payload*>
using PayloadPointer = T;

template<class T = const Payload*>
using ConstPayloadPointer = T;

template<class T = Payload**>
using PayloadPointerChain = T;

template<class T = left::Item*>
using LeftItemPointer = T;

enum class Mark : unsigned int {
	Live = 9
};

template<class T = Mark*>
using MarkPointer = T;

static_assert(__is_same(NativePointer<>, int*));
static_assert(__is_same(PayloadPointer<>, Payload*));
static_assert(__is_same(ConstPayloadPointer<>, const Payload*));
static_assert(__is_same(PayloadPointerChain<>, Payload**));
static_assert(__is_same(LeftItemPointer<>, left::Item*));
static_assert(!__is_same(LeftItemPointer<>, right::Item*));
static_assert(__is_same(::alias_defaults::NamespacedPointer<>, alias_defaults::Namespaced*));
static_assert(__is_same(MarkPointer<>, Mark*));

int native_value = 6;
Payload payload{17, 4};
Payload* payload_address = &payload;
left::Item left_item{8};
alias_defaults::Namespaced namespaced{12};
Mark mark = Mark::Live;

NativePointer<> native_pointer = &native_value;
PayloadPointer<> payload_pointer = &payload;
ConstPayloadPointer<> const_payload_pointer = &payload;
PayloadPointerChain<> payload_pointer_chain = &payload_address;
LeftItemPointer<> left_item_pointer = &left_item;
::alias_defaults::NamespacedPointer<> namespaced_pointer = &namespaced;
MarkPointer<> mark_pointer = &mark;

int main() {
	*native_pointer += 6;
	if (left_item_pointer->value != 8) {
		return 3;
	}
	if (namespaced_pointer->value != 12) {
		return 4;
	}
	return *native_pointer + payload_pointer->tag + static_cast<int>(*mark_pointer) +
		static_cast<int>(const_payload_pointer->value - payload_pointer->value) +
		static_cast<int>((**payload_pointer_chain).value - payload_pointer->value) +
		static_cast<int>(payload_pointer->value) == 42 ? 42 : 2;
}
