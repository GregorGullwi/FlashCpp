// More than 8192 temporary slots must not wrap the frame's size at 64 KiB.
#define STEPS_2(X) X X
#define STEPS_4(X) STEPS_2(STEPS_2(X))
#define STEPS_16(X) STEPS_4(STEPS_4(X))
#define STEPS_256(X) STEPS_16(STEPS_16(X))
#define STEPS_4096(X) STEPS_16(STEPS_256(X))

int keepLargeFrameInput(int value) {
	return value;
}

inline int exerciseLargeFrame(int input, long long wide, double fraction, float single, int extra) {
	// This call touches the bottom of the allocated frame before the arithmetic
	// gradually fills its slots, exercising the normal OS stack guard as well.
	int seed = keepLargeFrameInput(input);
	int total = 0;
	STEPS_4096(total = total + (seed * 3 + 1);)
	if (wide != 0x123456789LL || fraction != 2.5 || single != 1.25f || extra != 17) return 2;
	return total == 4096 * (input * 3 + 1) ? 0 : 1;
}

int main() {
	return exerciseLargeFrame(2, 0x123456789LL, 2.5, 1.25f, 17);
}
