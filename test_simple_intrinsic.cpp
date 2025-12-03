int test_is_void() {
    return __is_void(void) ? 1 : 0;
}

int main() {
    return test_is_void();
}
