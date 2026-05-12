// Test tuple-like structured binding when tuple_size<T> inherits value from integral_constant
// Expected return: 42

namespace std {
template <typename T, T v>
struct integral_constant {
static constexpr T value = v;
};

template <typename T>
struct tuple_size;

template <unsigned long I, typename T>
struct tuple_element;
} // namespace std

struct Triple {
int first;
int second;
int ignored;
};

namespace std {
template <>
struct tuple_size<Triple> : integral_constant<unsigned long, 2> {};

template <>
struct tuple_element<0, Triple> {
using type = int;
};

template <>
struct tuple_element<1, Triple> {
using type = int;
};
} // namespace std

template <unsigned long I>
int get(const Triple& p);

template <>
int get<0>(const Triple& p) {
return p.first;
}

template <>
int get<1>(const Triple& p) {
return p.second;
}

int main() {
Triple p;
p.first = 10;
p.second = 32;
p.ignored = 999;

auto [a, b] = p;
return a + b;
}
