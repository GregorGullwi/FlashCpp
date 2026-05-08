int pick(double*) { return 1; }
int pick(long double*) { return 2; }

int main() {
long double value = 0.0L;
return pick(&value);
}
