// Test: out-of-line member function definitions for template specializations
// Pattern: ReturnType ClassName<Args>::func(...) { ... }

template<typename T>
class Container {
public:
    T get() const;
    bool is_valid(T val) const;
    const T* find(const T* begin, const T* end, T val) const;
};

template<>
class Container<int> {
public:
    int get() const;
    bool is_valid(int val) const;
    const int* find(const int* begin, const int* end, int val) const;
};

// Out-of-line definitions for template specialization
int
Container<int>::
get() const
{ return 42; }

bool
Container<int>::
is_valid(int val) const
{ return val > 0; }

// Overloaded function with different parameter count
const int*
Container<int>::
find(const int* begin, const int* end, int val) const
{
    while (begin != end) {
        if (*begin == val) return begin;
        ++begin;
    }
    return end;
}

int main() {
    Container<int> c;
    if (c.get() != 42) return 1;
    if (!c.is_valid(10)) return 2;
    if (c.is_valid(-1)) return 3;
    return 42;
}
