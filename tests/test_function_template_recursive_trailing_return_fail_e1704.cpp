// SoftProbe/ADL retry storms used to continue forever after function-template
// depth soft-failed. A mutually recursive trailing-return decltype must still
// fail finitely (and, once the shared sticky instantiation budget trips on
// deeper storms, stop further attempts in the same translation unit).

template<class T>
auto ping(T x) -> decltype(pong(x));

template<class T>
auto pong(T x) -> decltype(ping(x));

int main() {
	return ping(0);
}
