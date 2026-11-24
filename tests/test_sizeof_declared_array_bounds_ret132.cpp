// [expr.sizeof]/2: sized arrays keep their real object sizes, unsized outer
// bounds still lack one, and array parameters decay to pointers before sizeof.
template <typename T>
concept Sized = requires { sizeof(T); };

static_assert(Sized<int[5]>);
static_assert(Sized<int[2][3]>);
static_assert(!Sized<int[][3]>);
static_assert(!Sized<int[]>);

struct Item {
	long long value;
	int tag;
};

int inferred[] = {1, 2, 3};
int two_dimensional[2][3];
int five[5];

int parameters(int row[][3], int flat[5]) {
	return static_cast<int>(sizeof(row)) + static_cast<int>(sizeof(flat));
}

int main() {
	int local_two_dimensional[2][3];
	int local_five[5];
	int local_inferred[] = {1, 2, 3, 4};

	int total = 0;
	total += static_cast<int>(sizeof(local_two_dimensional)); // 24
	total += static_cast<int>(sizeof(local_five));            // 20
	total += static_cast<int>(sizeof(local_inferred));        // 16
	total += static_cast<int>(sizeof(two_dimensional));       // 24
	total += static_cast<int>(sizeof(five));                  // 20
	total += static_cast<int>(sizeof(inferred));              // 12
	total += parameters(local_two_dimensional, local_five);   // 8 + 8
	return total;
}
