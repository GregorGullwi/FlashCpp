// Test: Can we parse qualified identifiers with template syntax?
// Simplest possible case

template<typename T>
struct Box {
    T data;
};

// Don't use it in an expression yet, just see if we can parse the declaration
Box<int> global_box;

int main() {
    return sizeof(global_box);
}
