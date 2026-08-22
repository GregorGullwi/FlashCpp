// Positive control: a member function of Box<int> MAY access private members
// of another object of the SAME specialization (Box<int>). Guards against
// over-restricting same-class access when distinguishing specializations.
template<typename T>
class Box {
public:
	void set(int v) { secret = v; }
	int copyFrom(const Box& other) { return other.secret; }
private:
	int secret;
};

int main() {
	Box<int> a;
	a.set(42);
	Box<int> b;
	return b.copyFrom(a);
}