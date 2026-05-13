struct Holder {
	using type = int;
};

template<typename T>
struct UseDependentTypename {
	typename T::type value;
};

int main() {
	UseDependentTypename<Holder> use;
	use.value = 42;
	return use.value;
}
