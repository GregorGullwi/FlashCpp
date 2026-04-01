namespace ns {
template <int N>
struct Outer {
	struct Inner {
		int tag = N;
	};

	Inner make() {
		return Inner{};
	}
};
}

int main() {
	ns::Outer<42> o;
	ns::Outer<42>::Inner obj = o.make();
	return obj.tag;
}
