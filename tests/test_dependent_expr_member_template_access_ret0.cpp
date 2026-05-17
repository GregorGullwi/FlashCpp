struct RebindCarrier {
	template <typename T>
	struct Rebind {
		static constexpr int value = sizeof(T);
	};
};

template <typename T>
struct UseExpr {
	static int value() {
		return T::template Rebind<int>::value;
	}
};

int main() {
	return UseExpr<RebindCarrier>::value() == (int)sizeof(int) ? 0 : 1;
}
