// A friend declaration naming a member class-template specialization through an
// instantiated owner chain (Outer<Args>::Box<char>) parses component-wise: the
// owner's template arguments are a type-system lookup key only, the member
// primary resolves by identity, and the member's own arguments are the granted
// specialization. Same-spelling member templates under different owners keep
// distinct instances and layouts.
struct PayloadA {
	int weight;
};

struct PayloadB {
	short height;
};

template <typename T>
struct WideOwner {
	template <typename U>
	struct Box {
		T outer;
		U inner;

		int width() const {
			return static_cast<int>(sizeof(T) + sizeof(U));
		}
	};
};

template <typename T>
struct NarrowOwner {
	template <typename U>
	struct Box {
		U inner;

		int width() const {
			return static_cast<int>(sizeof(U));
		}
	};
};

namespace ns {
template <typename T>
struct Holder {
	template <typename U>
	struct Slot {
		U value;
	};
};
}

struct HostA {
	friend struct WideOwner<long long>::Box<PayloadA>;
	int markerA = 1;
};

struct HostB {
	friend struct NarrowOwner<char>::Box<PayloadB>;
	int markerB = 2;
};

struct HostC {
	friend struct ns::Holder<int>::Slot<short>;
	int markerC = 3;
};

int main() {
	WideOwner<long long>::Box<PayloadA> wide{};
	NarrowOwner<char>::Box<PayloadB> narrow{};
	ns::Holder<int>::Slot<short> slot{7};

	if (wide.width() != static_cast<int>(sizeof(long long) + sizeof(PayloadA))) {
		return 1;
	}
	if (narrow.width() != static_cast<int>(sizeof(PayloadB))) {
		return 2;
	}
	if (slot.value != 7) {
		return 3;
	}
	wide.outer = 5;
	wide.inner.weight = 6;
	narrow.inner.height = 8;
	if (wide.outer != 5 || wide.inner.weight != 6) {
		return 4;
	}
	if (narrow.inner.height != 8) {
		return 5;
	}
	return 0;
}
