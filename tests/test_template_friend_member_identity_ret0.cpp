// A template friend declaration that names a member class-template
// specialization (template <...> friend struct Outer<T>::Box<int>) must parse
// component-wise and resolve the member primary by identity. It used to be a
// hard parse error (Expected ';' after template friend class declaration)
// because the legacy path consumed only the qualified spelling and stopped at
// the trailing template-id. The friend forms here cover a dependent owner with a
// concrete member argument and a concrete owner with a concrete member argument;
// same-spelling owners stay distinct.
struct FriendPayload {
	int weight;
};

template <typename T>
struct WideOwner {
	template <typename U>
	struct Box {
		T outer;
		U inner;
	};
};

template <typename T>
struct NarrowOwner {
	template <typename U>
	struct Box {
		short inner;
	};
};

struct HostA {
	template <typename T>
	friend struct WideOwner<T>::Box<int>;
	int markerA = 1;
};

struct HostB {
	template <typename U>
	friend struct WideOwner<int>::Box<FriendPayload>;
	int markerB = 2;
};

struct HostC {
	template <typename U>
	friend struct NarrowOwner<long long>::Box<U>;
	int markerC = 3;
};

int main() {
	WideOwner<long long>::Box<int> wide{};
	NarrowOwner<long long>::Box<FriendPayload> narrow{};
	static_assert(sizeof(WideOwner<int>::Box<FriendPayload>) > 0);
	HostA hostA;
	HostB hostB;
	HostC hostC;
	if (hostA.markerA != 1) {
		return 1;
	}
	if (hostB.markerB != 2) {
		return 2;
	}
	if (hostC.markerC != 3) {
		return 3;
	}
	if (&wide == nullptr || &narrow == nullptr) {
		return 4;
	}
	return 0;
}
