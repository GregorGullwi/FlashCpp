struct Value { int operator-() const { return 1; } };
int operator-(const Value&) { return 2; }
int main() { Value object; return -object; }
