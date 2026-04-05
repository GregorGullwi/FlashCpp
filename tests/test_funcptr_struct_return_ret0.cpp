struct Small {
	int x;
	int y;
};

struct Big {
	long long a;
	long long b;
	long long c;
};

Small makeSmall(int x, int y) {
	return Small{x, y};
}

Big makeBig(long long base) {
	return Big{base, base + 1, base + 2};
}

struct Holder {
	Small (*smallCb)(int, int);
	Big (*bigCb)(long long);
};

Holder makeHolder() {
	return Holder{makeSmall, makeBig};
}

int main() {
	Small (*smallFp)(int, int) = makeSmall;
	Small smallValue = smallFp(20, 22);
	int smallMemberTotal = smallFp(7, 9).x + makeHolder().smallCb(1, 2).y;
	if (smallValue.x != 20 || smallValue.y != 22) {
		return 1;
	}
	if (smallMemberTotal != 9) {
		return 2;
	}

	Big (*bigFp)(long long) = makeBig;
	Big bigValue = bigFp(10);
	Big bigFromMember = makeHolder().bigCb(4);
	if (bigValue.a != 10 || bigValue.b != 11 || bigValue.c != 12) {
		return 3;
	}
	if (bigFromMember.a != 4 || bigFromMember.b != 5 || bigFromMember.c != 6) {
		return 4;
	}

	return 0;
}
