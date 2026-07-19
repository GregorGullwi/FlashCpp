struct Payload {
	short head;
	long long tail;
};

struct ScalarVariant {
	union {
		Payload inactive;
		int tag{42};
	};
};

struct AggregateVariant {
	union {
		int inactive;
		Payload payload{7, 35};
	};
};

int main() {
	ScalarVariant scalar{};
	AggregateVariant aggregate{};
	return (scalar.tag - 42) + (aggregate.payload.head + aggregate.payload.tail - 42);
}
