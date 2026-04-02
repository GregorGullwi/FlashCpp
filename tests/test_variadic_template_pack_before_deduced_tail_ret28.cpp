template<typename... Ts, typename U>
int packBeforeTail(Ts... ts, U u) {
	return static_cast<int>(sizeof...(Ts) * 10 + sizeof(U));
}

int main() {
	return packBeforeTail<char, short>('a', (short)0, 0L);
}
