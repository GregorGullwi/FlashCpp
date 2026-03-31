// Test: increment/decrement and compound assignment on static member variables
// Reproduces the case where unqualified static member mutation inside a member
// function generated GlobalStore for the wrong StringHandle.

struct Counter {
	static int count;

	static void incrementTwice() {
		count++;
		count += 1;
	}
};

int Counter::count = 40;

int main() {
	Counter::incrementTwice();
	return Counter::count;
}
