struct Point {
    int x;
    int y;
};

int test_positional_init_only() {
    Point p = {10};
    return 0;
}


int main() {
    return test_positional_init_only();
}
