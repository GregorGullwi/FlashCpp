// Test basic function overload resolution by parameter type.
int add(int a, int b) { return a + b; }
double add(double a, double b) { return a + b; }

int main() {
	int result = add(20, 22);  // should select int overload
	return result;  // expect 42
}
