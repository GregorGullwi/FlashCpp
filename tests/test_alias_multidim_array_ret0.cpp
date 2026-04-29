// Test: multi-dimensional array alias templates
// Verifies that alias templates can carry and forward multi-dimensional array
// type arguments correctly.  This exercises the TemplateTypeArg::array_dimensions
// vector introduced to replace the single array_size optional field.
template<class T>
struct Box {
	T value;
};

// Alias template that passes the element type through unchanged.
template<class T>
using identity_t = T;

// Check that a Box over a 1-D array alias resolves correctly.
using IntArr4 = int[4];
Box<IntArr4> b1;  // global

int main() {
	b1.value[0] = 1;
	b1.value[1] = 2;
	b1.value[2] = 3;
	b1.value[3] = 4;
	if (b1.value[0] + b1.value[1] + b1.value[2] + b1.value[3] != 10)
		return 1;

	// Stack-local Box using identity alias
	Box<identity_t<IntArr4>> b2;
	b2.value[0] = 10;
	b2.value[1] = 20;
	b2.value[2] = 30;
	b2.value[3] = 40;
	if (b2.value[0] + b2.value[1] + b2.value[2] + b2.value[3] != 100)
		return 2;

	return 0;
}
