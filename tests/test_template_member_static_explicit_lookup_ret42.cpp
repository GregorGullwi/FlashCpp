template <class T>
struct Host {
	template <class U>
	static int value(U v) {
		return 40 + v;
	}

	static int run() {
		return Host::value<int>(2);
	}
};

int main() {
	return Host<void>::run();
}
