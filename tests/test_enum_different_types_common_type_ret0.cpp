enum SmallFlags : unsigned char {
	SmallOne = 1,
	SmallHigh = 250,
};

enum SignedState : int {
	SignedMinusOne = -1,
	SignedTwo = 2,
};

int main() {
	SmallFlags flags = SmallHigh;
	SignedState state = SignedMinusOne;

	// SmallHigh promotes to int 250, so it should compare greater than -1.
	if (!(flags > state))
		return 1;

	if ((flags + state) != 249)
		return 2;

	if ((SmallOne + SignedTwo) != 3)
		return 3;

	return 0;
}
