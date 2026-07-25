// C++20 [over.ics.rank]: prvalue RHS prefers move assignment for typedef unions
// (MSVC __m128i / wchar.h wmemchr pattern: __v1 = _mm_cmpeq_epi16(...)).
typedef union M128 {
	__int8 i8[16];
	__int16 i16[8];
	__int32 i32[4];
	__int64 i64[2];
} M128;

M128 make_m128() {
	M128 v{};
	v.i32[0] = 7;
	return v;
}

int main() {
	M128 a{};
	a = make_m128();
	return a.i32[0] == 7 ? 0 : 1;
}
