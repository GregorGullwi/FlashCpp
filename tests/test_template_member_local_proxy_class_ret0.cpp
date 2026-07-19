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
		return proxy.keep == value ? 42 : 0;
	}
};

int main() {
	common_like<int> int_iter{7};
	common_like<double> double_iter{3.5};
	return int_iter.read() == 42 && double_iter.read() == 42 ? 0 : 1;
}
