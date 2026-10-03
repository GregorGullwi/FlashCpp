template <class Type>
struct PrivateOwner {
private:
	int run(int value) const & {
		return value;
	}
};

using PrivateMember = int (PrivateOwner<int>::*)(int) const &;

int choosePrivate(const PrivateMember&) {
	return 0;
}

template <class Type>
auto formPrivateMemberAddress() {
	return choosePrivate(&PrivateOwner<Type>::run);
}

static_assert(sizeof(decltype(formPrivateMemberAddress<int>())) == sizeof(int));

int main() {
	return 0;
}
