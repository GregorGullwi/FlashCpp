// The built-in subscript form is commutative for array and pointer operands,
// so the parser-time constant-expression evaluator must accept index[array].

constexpr int values[5] = {11, 22, 33, 44, 55};
constexpr int reversed = 2[values];

static_assert(values[2] == 33);
static_assert(2[values] == 33);
static_assert(reversed == 33);
static_assert(4[values] == 55);

int main() {
	return 0;
}
