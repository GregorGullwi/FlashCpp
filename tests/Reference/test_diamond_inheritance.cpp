// Test diamond inheritance (without virtual base classes)
// This tests the classic diamond problem where a derived class
// inherits from two classes that both inherit from a common base

struct Base {
    int value;
    Base(int v) : value(v) {}
};

struct Left : public Base {
    int left_data;
    Left(int v, int l) : Base(v), left_data(l) {}
};

struct Right : public Base {
    int right_data;
    Right(int v, int r) : Base(v), right_data(r) {}
};

// Diamond: inherits from both Left and Right
// This will have TWO copies of Base (one through Left, one through Right)
struct Diamond : public Left, public Right {
    int diamond_data;
    Diamond(int v1, int v2, int l, int r, int d) 
        : Left(v1, l), Right(v2, r), diamond_data(d) {}
};

int main() {
    Diamond d(10, 20, 30, 40, 50);

    // Access members from Left path
    int left_value = d.left_data;  // 30

    // Access members from Right path
    int right_value = d.right_data;  // 40

    // Access diamond's own member
    int diamond_value = d.diamond_data;  // 50

    return left_value + right_value + diamond_value;  // Expected: 120
}

