template <int N>
struct Box {
	static constexpr int earlier = later;
	static constexpr int later = N + 1;
};

int main() {
	return Box<41>::earlier;
}
