// Test: __typeof on function names (GCC extension)
// Verifies that __typeof(function_name) resolves correctly
extern "C" int some_func(int);
extern "C" __typeof(some_func) another_func;

int global_var = 42;
__typeof(global_var) another_var = 10;

int main() {
	return another_var == 10 ? 0 : 1;
}
