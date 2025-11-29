// Test: Global struct declaration with variable

struct Point {
	int x = 1;
	int y = 2;
} q;

int main() {
	return q.x;  // Should return 1
}

