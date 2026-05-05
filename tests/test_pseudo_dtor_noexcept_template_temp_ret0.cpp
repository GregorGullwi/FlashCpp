template <bool NoThrow>
struct DtorFlag {
	int value;
	~DtorFlag() noexcept(NoThrow) {}
};

template <bool NoThrow>
struct Holder {
	DtorFlag<NoThrow> member;
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

	Holder<false> holder_false{};
	if (noexcept(holder_false.member.~DtorFlag<false>()))
		result |= 8;

	DtorFlag<false> arr_false[1]{};
	if (noexcept(arr_false[0].~DtorFlag<false>()))
		result |= 16;

	Holder<true> holder_true{};
	if (!noexcept(holder_true.member.~DtorFlag<true>()))
		result |= 32;

	DtorFlag<true> arr_true[1]{};
	if (!noexcept(arr_true[0].~DtorFlag<true>()))
		result |= 64;

	return result;
}
