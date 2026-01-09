// Test named anonymous struct (inline anonymous struct with member name)
// Pattern: struct { ... } member_name;

struct Container {
    struct {
        int x;
        int y;
    } point;
    
    int get_x() {
        return point.x;
    }
};

int main() {
    Container c;
    c.point.x = 42;
    c.point.y = 10;
    return c.point.x;  // Should return 42
}
