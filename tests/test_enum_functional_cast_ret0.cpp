// Test enum functional casts: EnumType(expr) per C++20 [expr.type.conv]

enum Color { Red = 0, Green = 1, Blue = 2 };
enum class Shape : int { Circle = 10, Square = 20 };

// Unscoped enum functional cast
int test_unscoped() {
    Color c = Color(1);
    if (c != Green) return 1;
    Color c2 = Color(0);
    if (c2 != Red) return 2;
    return 0;
}

// Scoped enum functional cast
int test_scoped() {
    Shape s = Shape(10);
    if (s != Shape::Circle) return 1;
    Shape s2 = Shape(20);
    if (s2 != Shape::Square) return 2;
    return 0;
}

// Typedef-to-enum functional cast
typedef Color MyColor;
int test_typedef_enum() {
    MyColor mc = MyColor(2);
    if (mc != Blue) return 1;
    return 0;
}

int main() {
    if (test_unscoped() != 0) return 1;
    if (test_scoped() != 0) return 2;
    if (test_typedef_enum() != 0) return 3;
    return 0;
}
