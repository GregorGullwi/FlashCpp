// Taking the address of a constexpr array element is a constant expression and
// emits a relocation with the element's byte addend.

constexpr int values[2] = {21, 22};
constexpr const int* element = &values[1];
static_assert(*element == 22, "constexpr array element address");

int main() {
	return *element == 22 ? 0 : 1;
}
