// Test new/delete in larger translation units with multiple functions and classes.
// Verifies that heap allocation uses correct calling convention on all platforms.

int helper1() { return 42; }
int helper2() { return 100; }
int helper3(int a, int b) { return a + b; }

class Dummy {
public:
    int x;
    Dummy() : x(0) {}
};

int main() {
    int* p = new int(42);
    int val = *p;
    delete p;

    int* arr = new int[5];
    arr[0] = 10;
    arr[1] = 20;
    int sum = arr[0] + arr[1];
    delete[] arr;

    Dummy d;
    int r = helper1() + helper2() + helper3(1, 2) + d.x;
    return (val == 42) && (sum == 30) && (r == 145) ? 0 : 1;
}
