// Reduced non-std stand-in for view_interface::empty()'s begin==end path.
// MSVC <iterator>/<ranges> hits the same pattern inside view_interface<_Derived>.

template <class It>
struct Sentinel {
	It end_;
};

template <class It>
constexpr bool operator==(It a, Sentinel<It> s) {
	return a.p == s.end_.p;
}

struct PtrIter {
	int* p;
};

// Minimal CRTP base matching view_interface::empty()'s forward_range branch.
template <class D>
struct view_iface {
	constexpr bool empty() {
		D& self = static_cast<D&>(*this);
		return self.begin() == self.end();
	}
};

struct MyView : view_iface<MyView> {
	int* begin_ = nullptr;
	int* end_ = nullptr;
	PtrIter begin() const { return PtrIter{begin_}; }
	Sentinel<PtrIter> end() const { return Sentinel<PtrIter>{PtrIter{end_}}; }
};

int main() {
	int xs[2] = {1, 2};
	MyView v;
	v.begin_ = xs;
	v.end_ = xs + 2;
	return v.empty() ? 1 : 0;
}
