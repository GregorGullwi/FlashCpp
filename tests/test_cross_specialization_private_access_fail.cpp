// Regression: cross-specialization private member access must be rejected.
// A member function of Box<int> must NOT be able to access the private
// member of a Box<double> object. The compiler previously lost the
// accessing specialization (Box<int>) during template replay / IR context
// setup and treated the access as if it came from Box<double> itself.
template<typename T>
class Box {
public:
	int poke(Box<double>& other) {
		return other.secret; // ERROR: Box<int> cannot access Box<double>::secret
	}
private:
	int secret;
};

int main() {
	Box<int> b;
	Box<double> d;
	return b.poke(d);
}