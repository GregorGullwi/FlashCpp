// Regression test: nested try/catch inside a catch body where both catch
// handlers fall through normally. Verifies the outer catch funclet still
// resumes correctly and the parent frame remains usable afterwards.

int main() {
	volatile int result = 0;
	try {
		throw 1;
	} catch (int outer_val) {
		result += outer_val;
		try {
			throw 2;
		} catch (int inner_val) {
			result += inner_val;
		}
		result += 10;
	}
	result += 100;
	if (result != 113)
		return 1;
	return 0;
}