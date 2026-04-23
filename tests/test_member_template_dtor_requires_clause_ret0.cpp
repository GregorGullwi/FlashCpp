// Regression: member class template destructor with a trailing `requires`
// clause must be skipped without corrupting the token stream.
//
// The old `skipMemberStructTemplateDestructor` loop did not include `requires`
// as a break token, so the first `{` inside the requires expression was
// consumed as the start of the destructor body, causing a parse error when
// the actual `{` body was later encountered.

template <typename _Up>
struct _Storage {
	~_Storage() requires requires { typename _Up; } { }
};

int main() { return 0; }
