// Test 1: Basic int type
template<typename T>
struct Container {
    static int value;
};

template<typename T>
int Container<T>::value = 42;

// Test 2: Different type per specialization
template<typename T>
struct TypeContainer {
    static T data;
};

template<typename T>
T TypeContainer<T>::data = T();

// Test 3: Multiple template parameters
template<typename T, typename U>
struct Pair {
    static int count;
};

template<typename T, typename U>
int Pair<T, U>::count = 100;

int main() {
    int result = 0;
    
    // Test 1
    result += (Container<int>::value == 42) ? 0 : 1;
    result += (Container<char>::value == 42) ? 0 : 2;
    
    // Test 2  
    result += (TypeContainer<int>::data == 0) ? 0 : 4;
    
    // Test 3
    result += (Pair<int, char>::count == 100) ? 0 : 8;
    
    return result;
}
