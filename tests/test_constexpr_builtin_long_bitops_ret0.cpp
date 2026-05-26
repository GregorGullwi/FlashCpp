static_assert(__builtin_clzl(1ul) == sizeof(long) * 8 - 1);
static_assert(__builtin_ctzl(8ul) == 3);
static_assert(__builtin_popcountl(0x13ul) == 3);
static_assert(__builtin_ffsl(0x10ul) == 5);

static_assert(__builtin_clzll(1ull) == sizeof(long long) * 8 - 1);
static_assert(__builtin_ctzll(16ull) == 4);
static_assert(__builtin_popcountll(0x2aull) == 3);
static_assert(__builtin_ffsll(0x20ull) == 6);

int main() {
	return 0;
}
