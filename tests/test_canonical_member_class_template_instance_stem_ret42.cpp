// Same-spelled member class templates under distinct owners must not share an
// instantiation-cache entry or generated owner name.
struct OuterA {
	struct Inner {
		template<class T>
		struct Meter {
			T value;
		};
	};
};

struct OuterB {
	struct Inner {
		template<class T>
		struct Meter {
			T value;
			long long extra;
		};
	};
};

using Narrow = OuterA::Inner::Meter<int>;
using Wide = OuterB::Inner::Meter<int>;

static_assert(sizeof(Narrow) != sizeof(Wide));

int main() {
	Narrow narrow{};
	Wide wide{};
	narrow.value = 7;
	wide.value = 11;
	wide.extra = 13;
	return narrow.value == 7 && wide.value == 11 && wide.extra == 13 ? 42 : 1;
}
