// Test: *this dereference in constexpr member function bodies

struct Vec2 {
    int x, y;
    constexpr Vec2(int x, int y) : x(x), y(y) {}
    constexpr int dot(const Vec2& other) const { return x * other.x + y * other.y; }
    constexpr int length_sq() const { return dot(*this); }
    constexpr Vec2 scale(int s) const { return Vec2{x * s, y * s}; }
    constexpr int dot_with_scaled(int s) const {
        Vec2 scaled = scale(s);
        return dot(scaled);
    }
};

constexpr Vec2 v(3, 4);
static_assert(v.length_sq() == 25);  // 3*3 + 4*4 = 25
static_assert(v.dot_with_scaled(2) == 50);  // dot(v, 2*v) = 3*6 + 4*8 = 18 + 32 = 50

int main() { return 0; }
