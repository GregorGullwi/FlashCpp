long long pick(short) { return 11; }
long long pick(long long) { return 22; }

namespace Other {
long long pick(short) { return 31; }
long long pick(long long) { return 32; }
}

int main() {
	long long (*long_target)(long long) = &pick;
	if (long_target(1LL) != 22)
		return 1;
	long long (*short_target)(short) = &pick;
	if (short_target(1) != 11)
		return 2;
	long long (*qualified_target)(long long) = &Other::pick;
	if (qualified_target(1LL) != 32)
		return 3;
	return 0;
}
