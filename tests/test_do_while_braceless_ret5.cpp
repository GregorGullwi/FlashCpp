// Test: Braceless do-while loop (body is a single statement without braces)
// This pattern appears in standard headers like <shared_mutex>
int main() {
	int x = 0;
	do
		x = x + 1;
	while (x < 5);
	return x;
}
