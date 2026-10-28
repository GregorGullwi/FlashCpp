namespace first {
	int pick(short value);
	int pick(short value) { return value + 1; }
	long long wide(long long value) { return value + 2; }
}

namespace second {
	int pick(short value) { return value + 3; }
}

int safe(short value) noexcept { return value + 4; }

int main() {
	int (*first_target)(short) = &first::pick;
	long long (*wide_target)(long long) = &first::wide;
	int (*second_target)(short) = &second::pick;
	int (*relaxed_target)(short) = &safe;
	if (first_target(1) != 2) return 1;
	if (wide_target(1LL) != 3) return 2;
	if (second_target(1) != 4) return 3;
	return relaxed_target(1) == 5 ? 0 : 4;
}
