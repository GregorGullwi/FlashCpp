struct Point { int x = 5; int y = 10; }; int main() { Point p; Point q = p; return q.x + q.y; }
