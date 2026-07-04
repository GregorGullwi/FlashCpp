using uint64_t = unsigned long long;

template <class T>
concept HasMsvcInt128Intrinsics = requires(
	unsigned char carry,
	uint64_t left,
	uint64_t right,
	uint64_t divisor,
	uint64_t* result) {
	_addcarry_u64(carry, left, right, result);
	_subborrow_u64(carry, left, right, result);
	_umul128(left, right, result);
	_udiv128(left, right, divisor, result);
};

int main() {
	static_assert(HasMsvcInt128Intrinsics<int>, "MSVC 128-bit helper intrinsics should be declared");
	return 0;
}
