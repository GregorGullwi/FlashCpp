// Test: Return statement with braced initializer - multiple members

struct Point
{
  int x;
  int y;
};

Point makePoint()
{
 return { .x = 10, .y = 20 };
}

int main() {
    Point p = makePoint();
    return p.x + p.y;  // Should return 30
}
