// Test: multi-dimensional array alias templates
// Verifies that alias templates correctly forward single- and multi-dimensional
// array type arguments.  This exercises the TemplateTypeArg::array_dimensions
// vector introduced to replace the single array_size optional field.
//
// All variables are stack-local to avoid pre-existing codegen limitations
// with global template struct instances that hold array members.

template<class T>
struct Box { T value; };

// Alias template that passes the element type through unchanged.
template<class T>
using identity_t = T;

// Verify that identity_t preserves the 1-D array type for specialisation matching.
template<class>
struct is_int_array3 {
	static constexpr bool value = false;
};
template<>
struct is_int_array3<int[3]> {
	static constexpr bool value = true;
};

using IntArr3 = int[3];
static_assert(is_int_array3<identity_t<IntArr3>>::value,
	"identity_t must preserve 1-D array type in template arg");

int main() {
	// 1-D array via alias — stack local Box<identity_t<IntArr3>>
	Box<identity_t<IntArr3>> b1;
	b1.value[0] = 10;
	b1.value[1] = 20;
	b1.value[2] = 30;
	if (b1.value[0] + b1.value[1] + b1.value[2] != 60)
		return 1;

	// 2-D array through identity alias — local variable
	using IntArr2x3 = int[2][3];
	identity_t<IntArr2x3> arr;
	arr[0][0] = 1; arr[0][1] = 2; arr[0][2] = 3;
	arr[1][0] = 4; arr[1][1] = 5; arr[1][2] = 6;
	int sum = arr[0][0] + arr[0][1] + arr[0][2]
	        + arr[1][0] + arr[1][1] + arr[1][2];
	if (sum != 21)
		return 2;

	return 0;
}
