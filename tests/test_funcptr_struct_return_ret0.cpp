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

	Big (*bigFp)(long long) = makeBig;
	Big bigValue = bigFp(10);
	Big bigFromMember = makeHolder().bigCb(4);

	long long total = 0;
	total += smallValue.x + smallValue.y;
	total += smallMemberTotal;
	total += bigValue.a + bigValue.b + bigValue.c;
	total += bigFromMember.a + bigFromMember.b + bigFromMember.c;

	return total == 100 ? 0 : 1;
}
