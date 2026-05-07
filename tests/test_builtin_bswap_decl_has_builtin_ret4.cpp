#if __has_builtin(__builtin_bswap16)
constexpr int has_bswap16 = 1;
#else
constexpr int has_bswap16 = 0;
#endif

#if __has_builtin(__builtin_bswap32)
constexpr int has_bswap32 = 1;
#else
constexpr int has_bswap32 = 0;
#endif

#if __has_builtin(__builtin_bswap64)
constexpr int has_bswap64 = 1;
#else
constexpr int has_bswap64 = 0;
#endif

#if __has_builtin(__builtin_ia32_pause)
constexpr int has_pause = 1;
#else
constexpr int has_pause = 0;
#endif

unsigned short use16(unsigned short x) {
	return __builtin_bswap16(x);
}

unsigned int use32(unsigned int x) {
	return __builtin_bswap32(x);
}

unsigned long long use64(unsigned long long x) {
	return __builtin_bswap64(x);
}

int main() {
	return has_bswap16 + has_bswap32 + has_bswap64 + has_pause;
}
