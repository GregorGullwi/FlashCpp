template <class Left, class Right, bool Enabled>
struct Reordered;

template <class First, class Second>
struct Reordered<Second, First, true> {
	static constexpr int first_size = sizeof(First);
	static constexpr int second_size = sizeof(Second);
};

struct Small {
	char value;
};

struct Large {
	long long values[2];
};

int main() {
	using Selected = Reordered<Small, Large, true>;
	return Selected::first_size == sizeof(Large) &&
		Selected::second_size == sizeof(Small)
		? 0
		: 1;
}
