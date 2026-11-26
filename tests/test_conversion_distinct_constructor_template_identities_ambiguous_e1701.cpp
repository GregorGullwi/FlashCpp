// C++20 [over.ics.rank]/3.3: two user-defined conversion sequences are ordered by
// their second standard conversion sequence only when they contain the same
// conversion function or constructor. Both targets reach their parameter through a
// constructor template, so both selections come from the declaration bridge. The
// bridge reports each selection's own owner and schema index, so First's
// constructor and Second's constructor stay distinct and the call is ambiguous;
// mapping every bridge selection to one shared identity would wrongly order them.
struct Source {
	int value;
};

struct First {
	template <class T>
	First(T source) : value(source.value) {}
	int value;
};

struct Second {
	template <class T>
	Second(T source) : value(source.value + 100) {}
	int value;
};

int choose(First&& target) {
	return target.value;
}

int choose(Second&& target) {
	return target.value;
}

int main() {
	return choose(Source{42});
}
