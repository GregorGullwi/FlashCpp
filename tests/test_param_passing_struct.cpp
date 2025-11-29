// Test struct parameter passing through functions

extern "C" int printf(const char*, ...);

struct Point {
    int x;
    int y;
};

void level2(Point p) {
    printf("level2: p.x=%d, p.y=%d\n", p.x, p.y);
}

void level1(Point p) {
    printf("level1: p.x=%d, p.y=%d\n", p.x, p.y);
    level2(p);
}

int main() {
    Point p;
    p.x = 100;
    p.y = 200;
    printf("main: calling level1 with Point{100, 200}\n");
    level1(p);
    return 0;
}
