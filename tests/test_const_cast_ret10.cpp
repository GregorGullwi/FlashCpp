int main() {
    const int cv = 10;
    const int* cptr = &cv;
    
    // Use const_cast to remove const
    int* ptr = const_cast<int*>(cptr);
    
    // Use the pointer
    int value = *ptr;
    return value;
}
