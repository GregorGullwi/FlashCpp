// Regression: Nest<N>::value = Nest<N-1>::value + 1 should evaluate to N.
// Previously, lazy static member instantiation skipped re-parsing the initializer
// because initializer_position was not saved for top-level template struct members,
// causing the value to always evaluate to 0.

template<int N>
struct Nest : Nest<N - 1> {
static constexpr int value = Nest<N - 1>::value + 1;
};
template<>
struct Nest<0> {
static constexpr int value = 0;
};
int main() {
return Nest<10>::value == 10 ? 0 : 1;
}
