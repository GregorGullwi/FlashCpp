struct Good { static constexpr bool valid = true; };
struct Bad { static constexpr bool valid = false; };

template<class T> struct Check { static_assert(T::valid, "later pack element must instantiate"); };
template<class... T> struct All : Check<T>... {};

All<Good, Bad> value;

int main() { return 0; }
