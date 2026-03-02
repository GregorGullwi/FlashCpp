// Test #line directive with filename
#line 100 "fake_file.cpp"
// After this, the presumed filename should be "fake_file.cpp" and line number 100

int main() {
    return 42;
}
