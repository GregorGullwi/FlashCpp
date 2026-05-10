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
