struct Pair {
	int first;
	int second;
};

struct Holder {
	static constexpr Pair pair{4, 5};
};

int main() {
	return Holder::pair.first + Holder::pair.second;
}
