template <class Type>
struct PrivateOwner {
private:
	template <class Argument>
	Type run(Argument) const &;
};

using PrivateMember = int (PrivateOwner<int>::*)(int) const &;

int choosePrivate(PrivateMember) {
	return 0;
}

template <class Type>
auto formPrivateMemberTemplateAddress() {
	return choosePrivate(&PrivateOwner<Type>::template run);
}

static_assert(sizeof(decltype(formPrivateMemberTemplateAddress<int>())) ==
	sizeof(int));

int main() {
	return 0;
}
