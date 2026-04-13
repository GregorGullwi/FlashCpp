struct PackBeforeTail {
	static int helper(char, short) {
		return 23;
	}

	static int helper() {
		return 0;
	}

	template<typename... Ts, typename Tail = int>
	static int count(Ts... values, Tail = 3) {
		return PackBeforeTail::helper(values...);
	}
};

int main() {
	return PackBeforeTail::count<char, short>(1, 2);
}
