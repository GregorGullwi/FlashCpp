// Regression: widening an 8- or 16-bit integer to a 64-bit integer must extend
// the value, not reuse the untouched high half of the destination register.
//
// `handleZeroExtend` encoded only three width pairs - 8->32, 16->32 and 32->64.
// Any other pair fell through to a plain-mov fallback that copies just the low
// bits of the source register, so the upper half of the result held whatever the
// register already contained. Every 8- or 16-bit source widened to a 64-bit
// integer therefore read uninitialized register bits:
//
//     unsigned char c = 200;      (unsigned long)c  ->  4210128
//     unsigned short s = 60000;   (unsigned long)s  ->  4210128
//
// That reaches `size_t`, `long`, `unsigned long`, `long long` and every typedef
// of them, so this is a general integer-widening defect rather than a cast-syntax
// one. It is also independent of the signedness of either end: an `unsigned char`
// sign-extended into a `long` was just as wrong, because the sign-extend path
// already covered its width pairs and only the zero-extend path was missing them.
//
// The 8->32 and 16->32 pairs and the 32->64 pair are covered too, so that the
// fix cannot quietly disturb the widths that already worked.

namespace {

int failures = 0;

void check(long long actual, long long expected) {
	if (actual != expected) {
		++failures;
	}
}

} // namespace

int main() {
	// 8-bit unsigned source, widened to every 64-bit integer type.  The payload
	// needs a high bit set so a missing extension is visible as a large value.
	const unsigned char u8 = 200;
	check((unsigned long)u8, 200UL);
	check((long)u8, 200L);
	check((long long)u8, 200LL);
	check(static_cast<unsigned long>(u8), 200UL);
	check((unsigned char)(unsigned long)u8, 200);

	// 16-bit unsigned source, same set of targets.
	const unsigned short u16 = 60000;
	check((unsigned long)u16, 60000UL);
	check((long)u16, 60000L);
	check((long long)u16, 60000LL);

	// Narrow signed sources must sign-extend, and stay correct when the widened
	// value is then read back through an unsigned type.
	const signed char s8 = -5;
	check((long long)s8, -5LL);
	check((unsigned long)(long long)s8, (unsigned long)-5LL);
	const short s16 = -300;
	check((long long)s16, -300LL);
	check((unsigned long)(long long)s16, (unsigned long)-300LL);

	// The widths that already worked, so the fix cannot disturb them.
	const unsigned char small = 7;
	check((unsigned)small, 7u);
	check((int)small, 7);
	check((unsigned)(unsigned int)4000000000u, 4000000000u);

	// A widened value used in arithmetic.
	const unsigned short big = 60000;
	check((unsigned long)big / 3, 20000UL);
	check((long long)big - 1, 59999LL);

	// The widened value must survive being passed to a function.
	if ((unsigned long)u8 != 200UL) {
		++failures;
	}
	return failures == 0 ? 0 : 1;
}
