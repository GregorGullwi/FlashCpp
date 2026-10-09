// Access control applies after overload ranking selects an implicit conversion
// operator for a call argument, so a private conversion function makes the call
// ill-formed even though it ranked as the best candidate.
template <class T>
struct Base {
	int value;
};

struct Source {
private:
	operator Base<int>() const { return Base<int>{}; }
};

int choose(Base<int>) { return 0; }

int main() { return choose(Source{}); }
