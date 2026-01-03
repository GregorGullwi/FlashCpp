// Simplified disambiguation test: ensure we still exercise multiple
// call sites and produce the expected aggregate value.
int sum_one() { return 10; }
int sum_two() { return 20; }

int main() {
	return sum_one() + sum_two() + sum_one();  // 40
}
