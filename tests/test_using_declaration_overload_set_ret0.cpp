namespace Util {
	int compute(int x) { return x - x; }      // returns 0 for any input
	int compute(int x, int y) { return x - y; }  // returns 0 when x==y
}

using Util::compute;

int main() {
	return compute(5);  // calls compute(int) -> returns 5-5 = 0
}
