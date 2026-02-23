// Test standard <latch> header
#include <latch>

int main() {
	std::latch l(1);
	l.count_down();
	l.wait();
	return 0;
}
