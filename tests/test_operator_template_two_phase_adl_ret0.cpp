struct Box {
	int value;
};

namespace noisy_ns {
	template<typename L, typename R>
	int operator+(const L&, const R&) {
		return -1;
	}
}

template<typename Dummy = void>
int operator+(const Box& lhs, const Box& rhs) {
	return lhs.value + rhs.value;
}

int main() {
	Box lhs{40};
	Box rhs{2};
	return lhs + rhs == 42 ? 0 : 1;
}
