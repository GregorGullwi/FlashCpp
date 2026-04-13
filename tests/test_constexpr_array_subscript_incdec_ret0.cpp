constexpr int testArraySubscriptIncDec() {
	int values[]{1, 2, 3};
	int index = 1;

	int prefix = ++values[index];
	int postfix = values[0]++;
	int prefix_dec = --values[2];
	int postfix_dec = values[index]--;

	if (prefix != 3)
		return 1;
	if (postfix != 1)
		return 2;
	if (prefix_dec != 2)
		return 3;
	if (postfix_dec != 3)
		return 4;
	if (values[0] != 2)
		return 5;
	if (values[1] != 2)
		return 6;
	if (values[2] != 2)
		return 7;

	return 0;
}

constexpr int arraySubscriptIncDecResult = testArraySubscriptIncDec();

static_assert(arraySubscriptIncDecResult == 0);

int main() {
	return arraySubscriptIncDecResult;
}
