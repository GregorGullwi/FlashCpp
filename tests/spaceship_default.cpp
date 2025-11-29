// Combined default spaceship operator tests
struct Point {
    int x, y;
    auto operator<=>(const Point&) const = default;
};

struct Inner {
    int value;
    auto operator<=>(const Inner&) const = default;
};

struct Outer {
    Inner member;
};

int main() {
    Point p1{1, 2};
    Point p2{1, 3};
    
    // Test synthesized comparison operators from operator<=>
    bool eq = p1 == p2;
    bool ne = p1 != p2;
    bool lt = p1 < p2;
    bool gt = p1 > p2;
    bool le = p1 <= p2;
    bool ge = p1 >= p2;
    
    Outer o1, o2;
    o1.member.value = 10;
    o2.member.value = 20;
    
    Inner m1 = o1.member;
    Inner m2 = o2.member;
    
    bool lt2 = m1 < m2;
    
    return 0;
}