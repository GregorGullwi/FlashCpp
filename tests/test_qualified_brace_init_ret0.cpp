// Test: Brace initialization with namespace-qualified types (ns::Type{args})
// This pattern appears in standard headers like <chrono>:
//   return chrono::duration<long double>{__secs};
namespace ns {
struct Point { int x; int y; };
}

ns::Point makePoint(int a, int b) {
return ns::Point{a, b};
}

int main() {
ns::Point p = makePoint(10, 20);
return p.x + p.y - 30;
}
