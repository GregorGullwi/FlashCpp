// Test: class template with outer pack + member function with inner pack
// Tests that outer pack (Ts) doesn't interfere with inner pack (Us) deduction
template<typename... Ts>
struct MultiStore {
template<typename... Us>
static int sum_inner(Us... args) {
return (0 + ... + args);
}
};

int main() {
// MultiStore<int,double> uses outer pack {int,double}, 
// sum_inner deduces inner pack {int,int,int}
int r = MultiStore<int, double>::sum_inner(14, 14, 14); // = 42
return r - 42;
}
