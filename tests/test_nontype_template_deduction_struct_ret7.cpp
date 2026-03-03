struct Point { int x; int y; };

template<typename T, int N>
struct Array { T data[N]; };

template<typename T, int N>
T getFirst(Array<T, N>& arr) { return arr.data[0]; }

int main() {
    Array<Point, 3> arr;
    arr.data[0].x = 7;
    arr.data[0].y = 2;
    Point p = getFirst(arr);  // T=Point (struct), N=3 both deduced from struct arg
    return p.x;  // Should return 7
}
