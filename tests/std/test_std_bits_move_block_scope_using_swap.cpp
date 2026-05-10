// This is a libstdc++-specific standard-header regression.  Including
// <bits/move.h> keeps the test focused on std::swap lookup without pulling in
// unrelated <utility> machinery that currently hits separate codegen gaps.
#include <bits/move.h>

struct SwapOwner {
	void swap(SwapOwner&) = delete;

	int run() {
		int left = 1;
		int right = 2;
		using std::swap;
		swap(left, right);
		return left == 2 && right == 1 ? 0 : 1;
	}
};

int main() {
	SwapOwner owner;
	return owner.run();
}
