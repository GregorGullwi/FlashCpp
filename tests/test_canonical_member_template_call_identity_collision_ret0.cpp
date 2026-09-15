struct HostA;
struct HostB;

struct OuterA {
	struct Inner {
		template <class T>
		struct Box {
			static int value();
		};
	};
};

struct OuterB {
	struct Inner {
		template <class T>
		struct Box {
			static int value();
		};
	};
};

struct HostA {
private:
	static int secret() { return 7; }
	friend struct OuterA::Inner::Box;
};

struct HostB {
private:
	static int secret() { return 11; }
	friend struct OuterB::Inner::Box;
};

template <class T>
int OuterA::Inner::Box<T>::value() {
	return HostA::secret();
}

template <class T>
int OuterB::Inner::Box<T>::value() {
	return HostB::secret();
}

int main() {
	return (OuterA::Inner::Box<int>::value() - 7) +
		(OuterB::Inner::Box<int>::value() - 11);
}
