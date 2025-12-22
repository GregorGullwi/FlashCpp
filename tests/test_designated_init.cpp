// Test cases for designated initializers

struct Point {
    int x;
    int y;
};

int test_designated_init_full() {
    Point p = {.x = 10, .y = 20};
    return p.x + p.y;  // Should return 30
}

int test_designated_init_partial() {
    Point p = {.y = 5};  // x should be 0
    return p.y;  // Should return 5
}

int test_designated_init_order() {
    Point p = {.y = 20, .x = 10};
    return p.x + p.y;  // Should return 30
}

int test_designated_init_single() {
    Point p = {.x = 42};
    return p.x;  // Should return 42
}

int test_designated_init_both_zero() {
    Point p = {.x = 0, .y = 0};
    return p.x + p.y;  // Should return 0
}

int test_designated_init_negative() {
    Point p = {.x = -5, .y = -10};
    return p.x + p.y;  // Should return -15
}

int test_designated_init_large() {
    Point p = {.x = 1000, .y = 2000};
    return p.x + p.y;  // Should return 3000
}

int test_designated_init_partial_first() {
    Point p = {.x = 15};  // y should be 0
    return p.x;  // Should return 15
}

int test_designated_init_mixed_values() {
    Point p = {.x = 7, .y = 3};
    return p.x * p.y;  // Should return 21
}

int test_designated_init_swap() {
    Point p = {.y = 100, .x = 50};
    return p.x - p.y;  // Should return -50
}


int main() {
    return test_designated_init_full();
}
