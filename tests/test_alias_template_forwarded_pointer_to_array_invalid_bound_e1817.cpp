template <class T, int N>
using ArrayPointer = T (*)[N];

template <class T, int N>
using ForwardedArrayPointer = ArrayPointer<T, N>;

template <class T, int N>
using TwiceForwardedArrayPointer = ForwardedArrayPointer<T, N>;

TwiceForwardedArrayPointer<int, 0> invalid_pointer;

int main() {
	return 0;
}
