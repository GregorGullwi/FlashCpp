// Test that a struct with multiple pointer conversion operators to different element
// types is rejected as ambiguous when used in a built-in subscript expression.
// C++20 [over.match.sub]: each viable conversion generates a separate built-in
// operator[] candidate; if multiple candidates result the call is ill-formed.

struct AmbiguousPtr {
	int int_data[2];
	short short_data[2];

	operator int*() { return &int_data[0]; }
	operator short*() { return &short_data[0]; }
};

int main() {
	AmbiguousPtr ap;
	return ap[0]; // error: ambiguous pointer conversion operators
}
