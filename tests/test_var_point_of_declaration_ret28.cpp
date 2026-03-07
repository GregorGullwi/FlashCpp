// Test point-of-declaration visibility inside variable initializers.
// Each declarator should be visible immediately after its own declarator,
// including global, local, static local, and comma-separated declarations.

int global_a = sizeof(global_a);
int global_b = sizeof(global_b), global_c = sizeof(global_c);

int main() {
	int local_a = sizeof(local_a);
	static int local_b = sizeof(local_b);
	int local_c = sizeof(local_c), local_d = sizeof(local_d);
	return global_a + global_b + global_c + local_a + local_b + local_c + local_d;
}