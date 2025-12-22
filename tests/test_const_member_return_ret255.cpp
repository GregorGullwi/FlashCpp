// Test const member function returning struct
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

struct Point {
    int x;
    int y;
    
    SimpleOrdering test() const {
        return SimpleOrdering(-1);
    }
};

int main() {
    Point p1{1, 2};
    SimpleOrdering result = p1.test();
    return result.value;
}
