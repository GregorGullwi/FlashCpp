// Test member function templates in template classes
template<typename T>
class Vector {
public:
    T data;
    
    template<typename U>
    U convert_and_set(U val) {
        data = static_cast<T>(val);
        return val;
    }
};

int main() {
    Vector<int> v;
    double result = v.convert_and_set(42.5);
    return static_cast<int>(result);
}
