// [over.match.ref] selects conversion functions that bind the target reference
// directly before the value-producing fallback is considered. For a const int&
// target operator int&() therefore wins over operator int(), which would need a
// temporary, even though both second sequences are identity conversions.
int direct_storage = 17;
int value_storage = 29;

struct PreferDirectReference {
	operator int&() { return direct_storage; }

	operator int() { return value_storage; }
};

int main() {
	PreferDirectReference holder;
	const int& alias = holder;
	return alias == 17 ? 33 : 1;
}
