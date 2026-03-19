// Test: constexpr struct with array member initialized via brace-init in constructor initializer list
// e.g., arr{a, b, c} correctly stores array values and supports subscript access

struct Triplet {
int vals[3];
constexpr Triplet(int a, int b, int c) : vals{a, b, c} {}
constexpr int get(int i) const { return vals[i]; }
};

// Access via member subscript (d.arr[i])
constexpr Triplet t(10, 20, 30);
static_assert(t.vals[0] == 10);
static_assert(t.vals[1] == 20);
static_assert(t.vals[2] == 30);

// Access via constexpr member function with literal index
static_assert(t.get(0) == 10);
static_assert(t.get(2) == 30);

// Access via constexpr member function with local variable index
struct Container {
int data[4];
constexpr Container() : data{1, 2, 3, 4} {}
constexpr int getSecond() const {
int idx = 1;
return data[idx];
}
constexpr int sum() const {
int s = 0;
for (int i = 0; i < 4; i++) s += data[i];
return s;
}
};

constexpr Container c{};
static_assert(c.data[0] == 1);
static_assert(c.data[3] == 4);
static_assert(c.getSecond() == 2);
static_assert(c.sum() == 10);

int main() { return 0; }
