// Test empty macro expansion
#define EMPTY
#define EMPTY_FUNC(x)

int EMPTY x = 10;
EMPTY_FUNC(foo)

int main() {
    return x;
}
