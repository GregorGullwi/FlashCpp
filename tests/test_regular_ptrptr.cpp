// Test regular double pointer without alias

int main() {
    int x = 42;
    int* p = &x;
    int** pp = &p;
    return **pp;
}
