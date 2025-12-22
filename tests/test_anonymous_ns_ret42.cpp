// Test anonymous namespace

namespace {
    int value = 42;
}

int test_anonymous() {
    return value;
}


int main() {
    return test_anonymous();
}
