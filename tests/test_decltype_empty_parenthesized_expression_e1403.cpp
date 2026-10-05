// An empty parenthesized expression is not a valid decltype operand, and the
// parenthesized-expression path must reject it rather than mis-scan the token
// range.
static_assert(__is_same(decltype(()), int), "empty parenthesized expression");

int main() {
	return 0;
}
