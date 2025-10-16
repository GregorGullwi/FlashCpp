// Simple test for struct with member function
struct Point {
    int x;
    int y;

    int getX() {
        return x;
    }
};

int main() {
    Point p;
    p.x = 5;
    p.y = 10;
    return p.getX();
}

