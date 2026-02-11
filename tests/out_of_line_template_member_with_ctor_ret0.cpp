// Test: out-of-line template member function definition with constructor in the class
// This verifies that try_parse_out_of_line_template_member correctly skips
// constructors/destructors when iterating member functions for signature validation.
// (The bug was that .as<FunctionDeclarationNode>() was called on constructor nodes
//  which are ConstructorDeclarationNode, causing bad_any_cast.)

template<typename T>
struct Container {
	using size_type = unsigned long;

	T* data_;
	size_type size_;

	// Constructor - stored as ConstructorDeclarationNode in member_functions_
	Container() : data_(nullptr), size_(0) {}

	// Regular member function
	size_type find(const T* str, size_type pos, size_type n) const;
};

// Out-of-line template member function definition
// When validating this against the class, the parser must skip
// the constructor node (which is NOT a FunctionDeclarationNode)
template<typename T>
    typename Container<T>::size_type
    Container<T>::
    find(const T* str, size_type pos, size_type n) const
    {
      if (n == 0)
        return pos <= size_ ? pos : 0;
      return 0;
    }

int main() {
	Container<char> c;
	const char* needle = "hello";
	// find with n=0 and pos=0 should return 0 since 0 <= size_(0)
	return c.find(needle, 0, 0);
}
