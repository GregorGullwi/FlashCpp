struct FlatGlobalItem {
	int value;
};

constexpr FlatGlobalItem items[] = {40, 2};

static_assert(items[0].value == 40);
static_assert(items[1].value == 2);

int main() {
	return (items[0].value == 40 && items[1].value == 2) ? 0 : 1;
}
