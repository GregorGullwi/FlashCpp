// Test: C++ aggregate-init zero-fill semantics for array members in constructor initializer lists
//
// C++ standard: in brace-init lists, elements not provided are zero-initialized.
// This applies to both single-element and partial-element brace-init of array members.

// 1. Single-element brace-init: arr{val} → arr[0]=val, rest=0
struct SingleInit {
	int arr[4];
	constexpr SingleInit() : arr{7} {}
};

constexpr SingleInit s{};
static_assert(s.arr[0] == 7);
static_assert(s.arr[1] == 0);
static_assert(s.arr[2] == 0);
static_assert(s.arr[3] == 0);

// 2. Partial brace-init: arr{a,b,c} for int arr[5] → arr[3] and arr[4] are 0
struct PartialInit {
int arr[5];
constexpr PartialInit() : arr{10, 20, 30} {}
};

constexpr PartialInit p{};
static_assert(p.arr[0] == 10);
static_assert(p.arr[1] == 20);
static_assert(p.arr[2] == 30);
static_assert(p.arr[3] == 0);
static_assert(p.arr[4] == 0);

// 3. Single-element with constructor parameter
struct WithParam {
int data[3];
constexpr WithParam(int v) : data{v} {}
};

constexpr WithParam w(42);
static_assert(w.data[0] == 42);
static_assert(w.data[1] == 0);
static_assert(w.data[2] == 0);

// 4. Sum to verify zero-fill doesn't produce garbage
struct Sumable {
int arr[4];
constexpr Sumable() : arr{1} {}  // Only first element = 1, rest = 0
constexpr int sum() const {
int s = 0;
for (int i = 0; i < 4; i++) s += arr[i];
return s;
}
};
constexpr Sumable sm{};
static_assert(sm.sum() == 1);  // Only arr[0]=1, rest are 0

int main() { return 0; }
