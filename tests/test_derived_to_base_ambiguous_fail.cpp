// An implicit conversion to an ambiguous base must be rejected rather than
// selecting whichever inheritance path happens to be visited first.

struct Base {
	int value;
};

struct Left : Base {};
struct Right : Base {};
struct Diamond : Left, Right {};

void take_base(Base value) {}

int main() {
	Diamond value;
	take_base(value);
	return 0;
}
