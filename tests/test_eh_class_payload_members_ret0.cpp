struct BytePayload {
	unsigned char v;
};

struct WordPayload {
	short a;
	unsigned char b;
};

struct WidePayload {
	long long high;
	int low;
	short tag;
};

int catchByteValue() {
	try {
		throw BytePayload{200};
	} catch (BytePayload payload) {
		return payload.v == 200 ? 0 : 11;
	}
	return 11;
}

int catchByteRef() {
	try {
		throw BytePayload{201};
	} catch (BytePayload& payload) {
		return payload.v == 201 ? 0 : 12;
	}
	return 12;
}

int catchWordValue() {
	try {
		throw WordPayload{300, 9};
	} catch (WordPayload payload) {
		if (payload.a != 300) return 13;
		if (payload.b != 9) return 14;
		return 0;
	}
	return 13;
}

int catchWordRef() {
	try {
		throw WordPayload{301, 10};
	} catch (WordPayload& payload) {
		if (payload.a != 301) return 15;
		if (payload.b != 10) return 16;
		return 0;
	}
	return 15;
}

int catchWideValue() {
	try {
		throw WidePayload{1234567890123LL, 7, 42};
	} catch (WidePayload payload) {
		if (payload.high != 1234567890123LL) return 5;
		if (payload.low != 7) return 6;
		if (payload.tag != 42) return 7;
		return 0;
	}
	return 5;
}

int catchWideRef() {
	try {
		throw WidePayload{1234567890123LL, 8, 43};
	} catch (WidePayload& payload) {
		if (payload.high != 1234567890123LL) return 8;
		if (payload.low != 8) return 9;
		if (payload.tag != 43) return 10;
		return 0;
	}
	return 8;
}

int catchMixedInOneFunction(int kind) {
	try {
		if (kind == 0) throw 11;
		if (kind == 1) throw BytePayload{55};
		throw WidePayload{99, 3, 4};
	} catch (int value) {
		return value == 11 ? 0 : 17;
	} catch (BytePayload byte_payload) {
		return byte_payload.v == 55 ? 0 : 18;
	} catch (WidePayload wide_payload) {
		if (wide_payload.high != 99) return 19;
		if (wide_payload.low != 3) return 20;
		if (wide_payload.tag != 4) return 21;
		return 0;
	}
	return 17;
}

int main() {
	int r0 = catchByteValue();
	if (r0 != 0) return r0;
	int r1 = catchByteRef();
	if (r1 != 0) return r1;
	int r2 = catchWordValue();
	if (r2 != 0) return r2;
	int r3 = catchWordRef();
	if (r3 != 0) return r3;
	int r4 = catchWideValue();
	if (r4 != 0) return r4;
	int r5 = catchWideRef();
	if (r5 != 0) return r5;
	int r6 = catchMixedInOneFunction(0);
	if (r6 != 0) return r6;
	int r7 = catchMixedInOneFunction(1);
	if (r7 != 0) return r7;
	int r8 = catchMixedInOneFunction(2);
	if (r8 != 0) return r8;
	return 0;
}
