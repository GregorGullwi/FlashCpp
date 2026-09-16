// An out-of-line plain member function of a member class template of a class
// template: the definition's inner template head belongs to the member class
// (Box), not to the function, so `scaled` is a plain member. Its body must
// attach to the instantiated member class and be emitted, not matched as a
// function template by the inner parameter count. Distinct same-spelling
// member classes under different owners stay distinct.
struct Payload {
	int weight;
};

template <typename T>
struct Owner {
	template <typename U>
	struct Box {
		T outer;
		U inner;

		int scaled() const;
	};
};

template <typename T>
struct OtherOwner {
	template <typename U>
	struct Box {
		U inner;

		int scale() const;
	};
};

template <typename T>
template <typename U>
int Owner<T>::Box<U>::scaled() const {
	return static_cast<int>(sizeof(T) + sizeof(U));
}

template <typename T>
template <typename U>
int OtherOwner<T>::Box<U>::scale() const {
	return static_cast<int>(sizeof(U));
}

int main() {
	Owner<long long>::Box<Payload> wide{};
	Owner<char>::Box<short> narrow{};
	OtherOwner<int>::Box<Payload> other{};

	if (wide.scaled() != static_cast<int>(sizeof(long long) + sizeof(Payload))) {
		return 1;
	}
	if (narrow.scaled() != static_cast<int>(sizeof(char) + sizeof(short))) {
		return 2;
	}
	if (other.scale() != static_cast<int>(sizeof(Payload))) {
		return 3;
	}
	return 0;
}
