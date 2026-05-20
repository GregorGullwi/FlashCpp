template <typename T>
struct Base {
template <typename U>
static constexpr int same_size_v = sizeof(T) == sizeof(U) ? 0 : 1;
};

template <typename T>
struct Derived : Base<T> {};

template <typename T>
constexpr int direct_member_v = Base<T>::template same_size_v<T>;

template <typename T>
constexpr int inherited_member_v = Derived<T>::template same_size_v<T>;

template <typename T>
struct Wrapper {
static constexpr int value = Derived<T>::template same_size_v<T>;
};

int main() {
static_assert(Base<int>::template same_size_v<int> == 0);
static_assert(direct_member_v<long> == 0);
static_assert(inherited_member_v<int> == 0);
static_assert(Wrapper<short>::value == 0);
return direct_member_v<long> + inherited_member_v<int> + Wrapper<short>::value;
}
