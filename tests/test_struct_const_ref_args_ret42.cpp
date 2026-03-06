// Test const& struct arguments and braced-init default arguments for const& params.
// Covers: free function, member function, template function, 8-byte and 12-byte structs.
struct Point { int x; int y; };                         // 8 bytes
struct Big3  { int a; int b; int c; };                  // 12 bytes

// 1. const& arg, no default
int sumPointRef(const Point& p)                       { return p.x + p.y; }

// 2. value arg, no default  (already tested elsewhere, included for completeness)
int sumPointVal(Point p)                              { return p.x + p.y; }

// 3. const& arg + braced-init default, 8-byte struct
int sumPointRefDef(const Point& p = {10, 32})         { return p.x + p.y; }

// 4. value arg + braced-init default, 8-byte struct
int sumPointValDef(Point p = {10, 32})                { return p.x + p.y; }

// 5. const& arg, 12-byte struct (previously crashed with two-register ABI mismatch)
int sumBig3Ref(const Big3& b)                         { return b.a + b.b + b.c; }

// 6. value arg, 12-byte struct (previously crashed with two-register ABI mismatch)
int sumBig3Val(Big3 b)                                { return b.a + b.b + b.c; }

// 7. const& + default, 12-byte struct
int sumBig3RefDef(const Big3& b = {10, 12, 20})       { return b.a + b.b + b.c; }

// 8. value + default, 12-byte struct
int sumBig3ValDef(Big3 b = {10, 12, 20})              { return b.a + b.b + b.c; }

// 9. Member function const& arg
struct Calc {
    int base;
    int addRef(const Point& p)                        { return base + p.x + p.y; }
    int addRefDef(const Point& p = {10, 32})          { return base + p.x + p.y; }
};

// 10. Template function with const& + default
template<typename T>
T tmplAddRef(const Point& p, T extra = 0) { return static_cast<T>(p.x + p.y) + extra; }

int main() {
    Point p; p.x = 20; p.y = 22;
    Big3 b; b.a = 10; b.b = 12; b.c = 20;

    if (sumPointRef(p) != 42) return 1;
    if (sumPointVal(p) != 42) return 2;
    if (sumPointRefDef() != 42) return 3;
    if (sumPointValDef() != 42) return 4;
    if (sumBig3Ref(b) != 42) return 5;
    if (sumBig3Val(b) != 42) return 6;
    if (sumBig3RefDef() != 42) return 7;
    if (sumBig3ValDef() != 42) return 8;

    Calc c; c.base = 0;
    if (c.addRef(p) != 42) return 9;
    if (c.addRefDef() != 42) return 10;

    if (tmplAddRef<int>(p) != 42) return 11;

    return 42;
}
