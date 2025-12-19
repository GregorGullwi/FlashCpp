// Test Named Return Value Optimization (NRVO)
// C++17 allows (but does not mandate) NRVO

struct Counter {
    int value;
    
    Counter(int v) : value(v) {}
};

// NRVO: returning named local variable
Counter makeCounter() {
    Counter c(42);
    c.value = c.value + 8;
    return c;
}

int main() {
    Counter result = makeCounter();
    return (result.value == 50) ? 0 : 1;
}
