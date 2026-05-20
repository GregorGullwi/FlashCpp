template <typename A, typename B>
struct Pair {
A first;
B second;
};

template <typename T>
struct Traits {
template <typename U>
struct Box {
template <typename V>
using Rebind = Pair<V, int>;
};
};

template <typename T>
struct Derived : Traits<T>::template Box<T>::template Rebind<T> {};

int main() {
return sizeof(Derived<char>) == sizeof(Pair<char, int>) ? 42 : 1;
}
