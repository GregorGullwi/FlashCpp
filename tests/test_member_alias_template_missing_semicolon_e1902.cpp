// A missing semicolon after a member alias template declaration must report the
// structured MissingSemicolon diagnostic instead of an unstructured error.
struct Holder {
	template <class T>
	using Identity = T
};

int main() { return 0; }
