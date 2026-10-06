int global_values[3] = {1, 2, 3};
constexpr int (&constexpr_global_ref)[] = global_values;
static_assert(&constexpr_global_ref[0] == &global_values[0]);

int main() {
	int local_values[3] = {4, 5, 6};
	int (&global_ref)[] = global_values;
	int (&local_ref)[] = local_values;
	if (&global_ref[0] != &global_values[0]) return 1;
	if (&local_ref[0] != &local_values[0]) return 2;
	return 0;
}
