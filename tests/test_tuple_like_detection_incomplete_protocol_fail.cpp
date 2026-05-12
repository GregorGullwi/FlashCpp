// Incomplete tuple-like protocol should be rejected.
// std::tuple_size alone is not enough; tuple_element/get are required.

namespace std {
template <typename T>
struct tuple_size;
}

struct MyPair {
int first;
int second;
};

namespace std {
template <>
struct tuple_size<MyPair> {
static constexpr size_t value = 2;
};
} // namespace std

int main() {
MyPair p;
p.first = 10;
p.second = 32;
auto [a, b] = p;
return a + b;
}
