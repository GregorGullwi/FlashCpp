// Test SimpleOrdering constructor directly
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

int main() {
    SimpleOrdering s1(-1);
    SimpleOrdering s2(1);
    SimpleOrdering s3(0);
    return s2.value;
}
