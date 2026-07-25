// Reduced MSVC emmintrin/wchar.h pattern: typedef union with multiple array
// members, then copy-assign (as in __m128i __v1 = ...; __v1 = __cmpeq(...);).
typedef union __declspec(align(16)) M128 {
	__int8 i8[16];
	__int16 i16[8];
	__int32 i32[4];
	__int64 i64[2];
} M128;

int main() {
	M128 a{};
	M128 b{};
	a.i32[0] = 42;
	b = a;
	return b.i32[0] == 42 ? 0 : 1;
}
