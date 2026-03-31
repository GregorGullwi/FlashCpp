// Test break and continue statements in constexpr functions

// break in for loop
constexpr int find_first_gt(int n) {
	int result = -1;
	for (int i = 0; i < 100; i++) {
		if (i * i > n) {
			result = i;
			break;
		}
	}
	return result;
}

static_assert(find_first_gt(10) == 4);
static_assert(find_first_gt(25) == 6);

// continue in for loop
constexpr int sum_odd(int n) {
	int sum = 0;
	for (int i = 1; i <= n; i++) {
		if (i % 2 == 0)
			continue;
		sum += i;
	}
	return sum;
}

static_assert(sum_odd(5) == 9);	// 1+3+5
static_assert(sum_odd(9) == 25);	 // 1+3+5+7+9

// break in while loop
constexpr int count_until(int n) {
	int count = 0;
	int i = 0;
	while (true) {
		if (i >= n)
			break;
		count++;
		i++;
	}
	return count;
}

static_assert(count_until(5) == 5);
static_assert(count_until(0) == 0);

int main() { return 0; }
