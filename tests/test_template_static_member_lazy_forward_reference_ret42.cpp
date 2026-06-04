template <int N>
struct Box {
	static constexpr int later = N + 1;
	static constexpr int earlier = later;
};

int main() {
	return Box<41>::earlier;
}
