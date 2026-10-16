int add_two(int value) { return value + 2; }
int (*select_adder(int which))(int) { (void)which; return &add_two; }
int (*(*make_selector(int which))(int))(int) { (void)which; return &select_adder; }
double add_fraction(double value) { return value + 0.5; }
double (*select_fraction(int which))(double) { (void)which; return &add_fraction; }
double (*(*make_fraction(char which))(int))(double) { (void)which; return &select_fraction; }

int main() {
	double (*(*(*local)(char))(int))(double) = &make_fraction;
	return make_selector(0)(1)(40) == 42 && local('x')(1)(41.5) == 42.0 ? 0 : 1;
}
