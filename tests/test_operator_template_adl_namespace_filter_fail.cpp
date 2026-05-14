namespace adl_ns {
	struct Tag {
		int value;
	};
}

template<typename T>
int evaluate_plus(const T& rhs) {
	adl_ns::Tag lhs{1};
	return lhs + rhs;
}

namespace noisy_ns {
	template<typename L, typename R>
	int operator+(const L&, const R&) {
		return -1;
	}
}

int main() {
	return evaluate_plus(2);
}
