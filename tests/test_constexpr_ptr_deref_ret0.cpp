// Test constexpr pointer dereference support

// Basic address-of and dereference
constexpr int global_val = 42;
constexpr const int* global_ptr = &global_val;
static_assert(*global_ptr == 42);

// Pointer passed as constexpr function argument
constexpr int deref(const int* p) {
return *p;
}
static_assert(deref(&global_val) == 42);

// Arrow member access through constexpr pointer to struct
struct Point {
int x;
int y;
constexpr Point(int a, int b) : x(a), y(b) {}
};

constexpr Point pt{10, 20};
constexpr const Point* ppt = &pt;
static_assert(ppt->x == 10);
static_assert(ppt->y == 20);

// Arrow access in constexpr function
constexpr int sum_via_arrow(const Point* p) {
return p->x + p->y;
}
static_assert(sum_via_arrow(&pt) == 30);

// Pointer branching inside constexpr function
constexpr int maybe_deref(const int* p, bool do_it) {
if (do_it) return *p;
return 0;
}
static_assert(maybe_deref(&global_val, true) == 42);
static_assert(maybe_deref(&global_val, false) == 0);

int main() {
return 0;
}
