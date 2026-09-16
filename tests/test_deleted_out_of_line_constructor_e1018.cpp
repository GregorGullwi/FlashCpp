// A deleted out-of-line constructor definition is not a first declaration.
struct Host {
	Host();
};

Host::Host() = delete;
