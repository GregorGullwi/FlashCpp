struct LongTag {
	char data[8];
};

LongTag tailTag(long);

template<typename... Ts, typename U>
auto packBeforeTail(U u) -> decltype(tailTag(u)) {
	return {};
}

int main() {
	packBeforeTail<char, short>(0L);
	return 0;
}
