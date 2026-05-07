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

using Bswap16Type = decltype(__builtin_bswap16((unsigned short)0));
using Bswap32Type = decltype(__builtin_bswap32((unsigned int)0));
using Bswap64Type = decltype(__builtin_bswap64((unsigned long long)0));

int main() {
	return has_bswap16 + has_bswap32 + has_bswap64 + has_pause;
}
