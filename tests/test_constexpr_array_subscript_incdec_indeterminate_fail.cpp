// Expected to fail: ++ on a default-initialized array element reads an
// indeterminate value during constexpr evaluation.
constexpr int testIndeterminateArrayIncDec() {
	int values[2];
	++values[0];
	return values[0];
}

static_assert(testIndeterminateArrayIncDec() == 0);

int main() {
	return 0;
}
