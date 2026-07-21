// Regression: an aggregate bound to an outer template parameter must retain
// all fields through nested value construction and current-instantiation copy.

struct Payload {
	long long wide;
	int narrow;
};

struct CompactPayload {
	int first;
	int second;
	int third;
};

struct DirectHolder {
	Payload current;

	explicit DirectHolder(Payload value) : current(value) {}
};

template<typename T>
struct Outer {
	struct Inner {
		T current;

		explicit Inner(T value) : current(value) {}
		Inner(const Inner& other) : current(other.current) {}

	};
};

int main() {
	Payload payload{0x123456789LL, 17};
	CompactPayload compact_payload{23, 29, 31};
	DirectHolder direct(payload);
	Outer<Payload>::Inner original(payload);
	Outer<Payload>::Inner constructor_copy(original);
	Outer<CompactPayload>::Inner compact_original(compact_payload);
	Outer<CompactPayload>::Inner compact_copy(compact_original);

	int result = 0;
	if (original.current.wide != payload.wide) result |= 1;
	if (original.current.narrow != payload.narrow) result |= 2;
	if (constructor_copy.current.wide != payload.wide) result |= 4;
	if (constructor_copy.current.narrow != payload.narrow) result |= 8;
	if (direct.current.wide != payload.wide) result |= 16;
	if (direct.current.narrow != payload.narrow) result |= 32;
	if (compact_original.current.first != compact_payload.first ||
		compact_original.current.second != compact_payload.second ||
		compact_original.current.third != compact_payload.third) result |= 64;
	if (compact_copy.current.first != compact_payload.first ||
		compact_copy.current.second != compact_payload.second ||
		compact_copy.current.third != compact_payload.third) result |= 128;
	return result;
}
