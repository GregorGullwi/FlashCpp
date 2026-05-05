template <bool NoThrow>
struct DtorFlag {
	int value;
	~DtorFlag() noexcept(NoThrow) {}
};

int main() {
	int result = 0;

	if (noexcept(DtorFlag<false>{}.~DtorFlag<false>()))
		result |= 1;

	if (!noexcept(DtorFlag<true>{}.~DtorFlag<true>()))
		result |= 2;

	DtorFlag<false> local_false{};
	if (noexcept(local_false.~DtorFlag<false>()))
		result |= 4;

	return result;
}
