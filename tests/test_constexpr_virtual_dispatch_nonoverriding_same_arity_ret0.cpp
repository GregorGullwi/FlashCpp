struct BaseDispatch {
	virtual constexpr int value(int) const {
		return 1;
	}
};

struct DerivedDispatch : BaseDispatch {
	constexpr int value(int) const override {
		return 42;
	}

	constexpr int value(long) const {
		return 7;
	}
};

constexpr int callBase(const BaseDispatch& base) {
	return base.value(0);
}

static_assert(callBase(DerivedDispatch{}) == 42);

int main() {
	return callBase(DerivedDispatch{}) == 42 ? 0 : 1;
}
