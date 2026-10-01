// Keep several bool-to-integer casts live through ordinary calls to exercise
// widening conversions as register allocation uses extended registers.
namespace {

int failures = 0;

void check(long long actual, long long expected) {
	if (actual != expected)
		++failures;
}

} // namespace

int exercise(bool yes) {
	check((int)yes, 1);
	check((unsigned)yes, 1u);
	check((long)yes, 1L);
	check((long long)yes, 1LL);
	check((unsigned)yes, 1u);
	check((unsigned)yes, 1u);
	check((unsigned)yes, 1u);
	check((unsigned)yes, 1u);
	return failures;
}

int main() {
	return exercise(true);
}
