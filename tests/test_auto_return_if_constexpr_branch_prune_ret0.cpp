template <bool Common>
struct range_like {
	auto end() {
		if constexpr (Common) {
			return 1;
		} else {
			return 2LL;
		}
	}
};

int main() {
	range_like<true> common;
	range_like<false> non_common;
	auto it = common.end();
	auto last = non_common.end();
	return (it - 1) + static_cast<int>(last - 2);
}
