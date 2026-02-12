// Test: Trailing semicolons after namespace closing brace (C++ empty-declaration)
// This pattern appears in standard headers like <chrono>:
//   namespace filesystem { struct __file_clock; };
namespace ns1 { struct Foo { int x; }; };
namespace ns2 { int val = 42; };

int main() {
ns1::Foo f;
f.x = ns2::val;
return f.x - 42;
}
