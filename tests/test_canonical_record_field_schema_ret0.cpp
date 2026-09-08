// Published complete records publish member/base field schemas when every
// member and direct base imports as a Supported canonical type. Mixing native
// sizes with a struct base proves schema publication does not abort compilation.
struct BasePayload {
	short tag;
};

struct DerivedPayload : BasePayload {
	int count;
	double* values;
};

int readPayload(const DerivedPayload& value) {
	return static_cast<int>(value.tag) + value.count;
}

int main() {
	double sample = 1.5;
	DerivedPayload value{};
	value.tag = 3;
	value.count = 5;
	value.values = &sample;
	return readPayload(value) - 8;
}
