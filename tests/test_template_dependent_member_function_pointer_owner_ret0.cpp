struct LeftOwner {};
struct RightOwner {};

struct LeftFunctionHolder {
	long long (LeftOwner::*member)(short, long) const & noexcept;
};

struct RightFunctionHolder {
	long long (RightOwner::*member)(short, long) const & noexcept;
};

template <typename Owner>
struct MemberFunctionHolder {
	long long (Owner::*member)(short, long) const & noexcept;
};

static_assert(__is_same(
	decltype(MemberFunctionHolder<LeftOwner>::member),
	decltype(LeftFunctionHolder::member)));
static_assert(__is_same(
	decltype(MemberFunctionHolder<RightOwner>::member),
	decltype(RightFunctionHolder::member)));
static_assert(!__is_same(
	decltype(MemberFunctionHolder<LeftOwner>::member),
	decltype(MemberFunctionHolder<RightOwner>::member)));

int main() {
	return __is_same(
			   decltype(MemberFunctionHolder<LeftOwner>::member),
			   decltype(LeftFunctionHolder::member)) &&
			   __is_same(
				   decltype(MemberFunctionHolder<RightOwner>::member),
				   decltype(RightFunctionHolder::member)) &&
			   !__is_same(
				   decltype(MemberFunctionHolder<LeftOwner>::member),
				   decltype(MemberFunctionHolder<RightOwner>::member))
		? 0
		: 1;
}
