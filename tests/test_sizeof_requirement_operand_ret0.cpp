// A simple requirement is satisfied only when its expression is well-formed.
// sizeof has no object size for void, an incomplete record, or an array of
// unknown bound ([expr.sizeof]/2), so those operands must report the
// requirement as unsatisfied through the shared sizeof-operand rule rather than
// being accepted from the expression's inferred type.

struct Incomplete;

template <typename T>
struct Complete {
	int a;
};

template <typename T>
concept Sized = requires { sizeof(T); };

int main() {
	int result = 0;
	if (!Sized<int>) result |= 1;
	if (!Sized<int[5]>) result |= 2;
	if (Sized<int[]>) result |= 4;
	if (Sized<void>) result |= 8;
	if (Sized<Incomplete>) result |= 16;
	if (!Sized<Complete<long long>>) result |= 32;
	if (!Sized<long long[2][3]>) result |= 64;
	return result;
}
