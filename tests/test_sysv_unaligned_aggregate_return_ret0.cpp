// 8-byte unaligned aggregate: Win64 returns in RAX; SysV classifies MEMORY and
// must use a hidden return slot rather than the size-based direct convention.
#pragma pack(push, 1)
struct PackedMisaligned {
	char tag;
	int value;
	char pad[3];
};
#pragma pack(pop)

static_assert(sizeof(PackedMisaligned) == 8);

PackedMisaligned make_packed(char tag, int value) {
	PackedMisaligned result;
	result.tag = tag;
	result.value = value;
	result.pad[0] = 1;
	result.pad[1] = 2;
	result.pad[2] = 3;
	return result;
}

template <typename T>
T make_templated(char tag, int value) {
	T result;
	result.tag = tag;
	result.value = value;
	result.pad[0] = 1;
	result.pad[1] = 2;
	result.pad[2] = 3;
	return result;
}

struct Holder {
	PackedMisaligned make(char tag, int value) {
		return make_packed(tag, value);
	}
};

int main() {
	if (sizeof(PackedMisaligned) != 8) {
		return 100 + (int)sizeof(PackedMisaligned);
	}

	PackedMisaligned made = make_packed(10, 32);
	if (made.tag != 10) {
		return 1;
	}
	if (made.value != 32) {
		return 2;
	}
	if (made.pad[0] != 1 || made.pad[1] != 2 || made.pad[2] != 3) {
		return 3;
	}

	PackedMisaligned templated = make_templated<PackedMisaligned>(7, 11);
	if (templated.tag != 7) {
		return 4;
	}
	if (templated.value != 11) {
		return 5;
	}

	Holder holder;
	PackedMisaligned member = holder.make(3, 9);
	if (member.tag != 3) {
		return 6;
	}
	if (member.value != 9) {
		return 7;
	}

	auto lambda_make = [](char tag, int value) -> PackedMisaligned {
		return make_packed(tag, value);
	};
	PackedMisaligned from_lambda = lambda_make(5, 42);
	if (from_lambda.tag != 5) {
		return 8;
	}
	if (from_lambda.value != 42) {
		return 9;
	}

	return 0;
}
