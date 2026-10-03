template <class T>
using ArrayRef = T (&)[0];

ArrayRef<int> invalid_reference;

int main() {
	return 0;
}
