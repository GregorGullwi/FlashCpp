// Test constructor call code generation

extern "C" int printf(const char*, ...);

class Point {
public:
    int x_;
    int y_;
    
    Point(int x, int y) {
        x_ = x;
        y_ = y;
        printf("Point(%d, %d)\n", x, y);
    }
};

Point global_point = Point(1, 2);

int main() {
    printf("Done\n");
    return 0;
}
