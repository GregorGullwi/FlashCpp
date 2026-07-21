// Regression: dereferencing a pointer to a complete aggregate must preserve
// its canonical object size through copy construction and non-RVO return.

struct Payload {
	long long wide;
	int narrow;
};

struct CompactPayload {
	int first;
	int second;
	int third;
};

using PayloadAlias = Payload;

template<typename T>
using Identity = T;

using ChainedPayloadAlias = Identity<PayloadAlias>;

Payload return_payload(Payload value) {
	Payload copy = value;
	return copy;
}

CompactPayload return_compact(CompactPayload value) {
	CompactPayload copy = value;
	return copy;
}

Payload return_pointer_copy(const Payload* value) {
	Payload copy = *value;
	return copy;
}

ChainedPayloadAlias return_alias_pointer_copy(const ChainedPayloadAlias* value) {
	ChainedPayloadAlias copy = *value;
	return copy;
}

template<typename T>
struct Outer {
	struct Inner {
		T current;

		explicit Inner(T value) : current(value) {}
		Inner(const Inner& other) : current(other.current) {}

		Inner copy_self() const {
			Inner copy = *this;
			return copy;
		}
	};
};

int main() {
	Payload payload{0x123456789LL, 17};
	CompactPayload compact{23, 29, 31};
	Payload returned_payload = return_payload(payload);
	CompactPayload returned_compact = return_compact(compact);
	Payload returned_pointer_copy = return_pointer_copy(&payload);
	ChainedPayloadAlias returned_alias_pointer_copy = return_alias_pointer_copy(&payload);
	Outer<Payload>::Inner original(payload);
	Outer<Payload>::Inner returned_inner = original.copy_self();

	int result = 0;
	if (returned_payload.wide != payload.wide) result |= 1;
	if (returned_payload.narrow != payload.narrow) result |= 2;
	if (returned_compact.first != compact.first) result |= 4;
	if (returned_compact.second != compact.second) result |= 8;
	if (returned_compact.third != compact.third) result |= 16;
	if (returned_inner.current.wide != payload.wide) result |= 32;
	if (returned_inner.current.narrow != payload.narrow) result |= 64;
	if (returned_pointer_copy.wide != payload.wide ||
		returned_pointer_copy.narrow != payload.narrow) result |= 128;
	if (returned_alias_pointer_copy.wide != payload.wide ||
		returned_alias_pointer_copy.narrow != payload.narrow) result |= 256;
	return result;
}
