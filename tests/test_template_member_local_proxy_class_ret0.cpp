template<typename Iter>
struct common_like {
	Iter value;

	int read() {
		struct ProxyBase {
			Iter keep;
			explicit ProxyBase(Iter value) : keep(value) {}
		};

		struct ArrowProxy : private ProxyBase {
			friend common_like;
			using ProxyBase::ProxyBase;
		};

		ArrowProxy proxy{value};
		return 42;
	}
};

int main() {
	common_like<int> iter{42};
	return iter.read() == 42 ? 0 : 1;
}
