// Test unnamed variadic parameter pack in member struct template
class Base {
public:
    template<typename...> 
    struct List { };
};

int main() {
    Base::List<int, float, double> l;
    return 0;
}
