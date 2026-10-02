// Regression: destructor cleanup must use declaration identity when locals
// shadow one another in nested, sequential, and exception-handler scopes.
int g_destroy_order = 0;

struct Tracked {
	int marker;

	explicit Tracked(int value) : marker(value) {}

	~Tracked() {
		g_destroy_order = g_destroy_order * 10 + marker;
	}
};

int main() {
	{
		Tracked item(1);
		{
			Tracked item(2);
		}
		if (g_destroy_order != 2) return 1;
	}
	if (g_destroy_order != 21) return 2;

	{
		Tracked item(3);
	}
	if (g_destroy_order != 213) return 3;
	{
		Tracked item(4);
	}
	if (g_destroy_order != 2134) return 4;

	try {
		Tracked item(5);
		throw 42;
	} catch (int item) {
		if (item != 42 || g_destroy_order != 21345) return 5;
		Tracked guard(6);
	}
	if (g_destroy_order != 213456) return 6;
	return 0;
}
