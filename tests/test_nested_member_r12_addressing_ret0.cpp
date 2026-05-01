// Regression test for nested member loads/stores through address-carrying temps.
// This used to misencode [R12]/[R12+disp] memory operands as indexed addressing,
// causing crashes after unrelated array traffic left a stale value in RAX.

struct Payload {
	int a;
	int b;
};

struct BigPayload {
	int values[8];
};

struct SmallBox {
	Payload stored;
};

struct BigBox {
	BigPayload stored;
};

int main() {
	SmallBox small{};
	BigBox big{};

	const bool small_ok = sizeof(SmallBox) >= sizeof(Payload);
	const bool big_ok = sizeof(BigBox) >= sizeof(BigPayload);

	small.stored.a = 1;
	small.stored.b = 2;
	big.stored.values[0] = 3;

	return (small_ok && big_ok && small.stored.a + small.stored.b + big.stored.values[0] == 6) ? 0 : 1;
}
