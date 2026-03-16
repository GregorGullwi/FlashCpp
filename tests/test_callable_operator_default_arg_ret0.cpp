struct AddForty {
    int operator()(int value, int extra = 40) const {
        return value + extra;
    }
};

auto invoke(AddForty callable) {
    return callable(2);
}

int main() {
    return invoke(AddForty{}) == 42 ? 0 : 1;
}
