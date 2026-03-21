// Regression: inline static constexpr aggregate members of struct type must
// materialize their initializer bytes for runtime loads, not read back as zero.

struct Holder {
	struct Payload {
		int a;
		int b;
	};

	static constexpr Payload payload = {2, 40};
};

int main() {
	return Holder::payload.a + Holder::payload.b;
}
