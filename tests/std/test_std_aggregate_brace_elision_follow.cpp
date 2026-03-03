// Aggregate brace elision with following members
struct AggregateWithTail {
    int arr[3];
    int tail;
};

int main() {
    AggregateWithTail v = {10, 20, 30, 42};
    return (v.arr[0] == 10 && v.arr[1] == 20 && v.arr[2] == 30 && v.tail == 42) ? 0 : 1;
}
