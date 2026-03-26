template<typename T>
struct AliasChain {
	using base_type = T;
	typedef base_type mid_type;
	using final_type = mid_type;
};

struct Payload {
	int value;
};

int selectScalar(int value) {
	return value;
}

int selectScalar(long value) {
	return (int)(value + 100);
}

int selectPayload(Payload* value) {
	return value->value;
}

int selectPayload(const void* value) {
	return ((const Payload*)value)->value + 100;
}

int main() {
	AliasChain<int>::final_type scalar = 7;
	AliasChain<Payload>::final_type payload{42};

	if (selectScalar(scalar) != 7) return 1;
	if (selectPayload(&payload) != 42) return 2;

	return 0;
}
