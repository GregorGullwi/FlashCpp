struct Point {
int x;
int y;
Point(int a, int b) : x(a), y(b) {}
};

int main() {
Point* p = new Point(10, 20);
delete p;
return 0;
}
