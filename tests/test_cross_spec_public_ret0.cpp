// Positive control: public member access across different specializations of
// the same class template must keep working. Only private/protected access
// rules distinguish specializations.
template<typename T>
class Pub {
public:
	int value;
};

template<typename T>
class Reader {
public:
	int read(Pub<double>& p) { return p.value; }
	void store(Pub<double>& p, int v) { p.value = v; }
private:
	int spare;
};

int main() {
	Pub<double> p{5};
	Reader<int> r;
	r.store(p, 7);
	return r.read(p) == 7 ? 0 : 1;
}