int seedvalue;
int seed() { return seedvalue; }

struct Holder { long long value = seed(); };
struct Wrap { Holder inner; };

int main() {
	seedvalue = -37;
	Holder negative_leaf{};
	Wrap negative_nested{};
	seedvalue = 37;
	Holder positive_leaf{};
	Wrap positive_nested{};
	const bool negative_ok = negative_leaf.value == -37 && negative_nested.inner.value == -37;
	const bool positive_ok = positive_leaf.value == 37 && positive_nested.inner.value == 37;
	return negative_ok && positive_ok ? 0 : 1;
}
