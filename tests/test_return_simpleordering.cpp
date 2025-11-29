// Test returning SimpleOrdering from a regular function
struct SimpleOrdering {
    int value;
    
    SimpleOrdering(int v) : value(v) {}
};

SimpleOrdering makeOrdering(int v) {
    return SimpleOrdering(v);
}

int main() {
    SimpleOrdering result = makeOrdering(-1);
    return result.value;
}
