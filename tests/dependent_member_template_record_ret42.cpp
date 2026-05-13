template<typename T>
struct Traits {
	template<typename U>
	struct rebind {
		using type = U;
	};
};

template<typename T, typename U>
struct UseMemberTemplate {
	typename Traits<T>::template rebind<U>::type value;
};

int main() {
	UseMemberTemplate<char, int> use;
	use.value = 42;
	return use.value;
}
