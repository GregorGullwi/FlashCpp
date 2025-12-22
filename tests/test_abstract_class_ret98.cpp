// Test abstract classes with pure virtual functions

// Abstract base class with pure virtual function
struct Shape {
    virtual int area() = 0;  // Pure virtual function
};

// Concrete derived class that implements the pure virtual function
struct Rectangle : public Shape {
    int width;
    int height;

    Rectangle(int w, int h) : width(w), height(h) {}

    int area() override {
        return width * height;
    }
};

// Another concrete derived class
struct Circle : public Shape {
    int radius;

    Circle(int r) : radius(r) {}

    int area() override {
        // Approximate area using integer arithmetic: pi * r^2 ≈ 3 * r^2
        return 3 * radius * radius;
    }
};

int main() {
    // This should work: instantiate concrete classes
    Rectangle rect(5, 10);
    Circle circ(4);

    // This should work: call virtual functions through base pointer
    Shape* s1 = &rect;
    Shape* s2 = &circ;

    int rect_area = s1->area();  // 50
    int circ_area = s2->area();  // 48 (3 * 4 * 4)

    // This should NOT compile: cannot instantiate abstract class
    // Shape s;  // Error: Cannot instantiate abstract class 'Shape'

    return rect_area + circ_area;  // Expected: 98
}

