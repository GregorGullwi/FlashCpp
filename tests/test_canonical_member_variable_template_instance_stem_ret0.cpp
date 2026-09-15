// Two same-spelling member variable templates under distinct published owners
// must never share an instantiation-cache entry, even with identical template
// arguments. The legacy instance name was keyed by the bare member stem
// ("Meter$<args-hash>"), so the second use returned the first owner's cached
// instance. The instance key stem is now collision-disambiguated with
// `$td<TemplateDeclId>`, exactly like the class-template instance bridge,
// while identity resolution answers each owner's own published template.
struct OuterA {
	struct Inner {
		template <int N>
		static constexpr int Meter = N + 1000;
	};
};

struct OuterB {
	struct Inner {
		template <int N>
		static constexpr int Meter = N;
	};
};

// Type-sensitive values: T-dependent initializers must substitute the use
// site's own argument per owner, never the other owner's cached argument.
struct WidthA {
	struct Inner {
		template <class T>
		static constexpr int Size = int(sizeof(T));
	};
};

struct WidthB {
	struct Inner {
		template <class T>
		static constexpr int Size = int(sizeof(T)) * 100;
	};
};

int main() {
	constexpr int a = OuterA::Inner::Meter<7>;
	constexpr int b = OuterB::Inner::Meter<7>;
	constexpr int c = WidthA::Inner::Size<char>;
	constexpr int d = WidthB::Inner::Size<int>;
	return (a - 1007) + (b - 7) + (c - 1) + (d - 400);
}
