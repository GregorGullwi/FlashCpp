// Closer to MSVC numeric_limits<long double>::infinity / quiet_NaN / signaling_NaN
long double infinity_ld() {
	return __builtin_huge_val();
}

long double quiet_nan_ld() {
	return __builtin_nan("0");
}

long double signaling_nan_ld() {
	return __builtin_nans("1");
}

struct LimitsLike {
	static long double infinity() noexcept {
		return __builtin_huge_val();
	}
	static long double quiet_NaN() noexcept {
		return __builtin_nan("0");
	}
	static long double signaling_NaN() noexcept {
		return __builtin_nans("1");
	}
};

int main() {
	volatile long double a = infinity_ld();
	volatile long double b = quiet_nan_ld();
	volatile long double c = signaling_nan_ld();
	volatile long double d = LimitsLike::infinity();
	volatile long double e = LimitsLike::quiet_NaN();
	volatile long double f = LimitsLike::signaling_NaN();
	(void)a;
	(void)b;
	(void)c;
	(void)d;
	(void)e;
	(void)f;
	return 0;
}
