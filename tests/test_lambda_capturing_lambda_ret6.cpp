int main() {
    auto inner = [](auto x) { return x + 1; };
    auto outer = [inner]() { 
        return inner(5);  
    };
	auto outer_neg = [&]() { 
        return -outer();  
    };
	if (outer_neg() != -6) return 1;
    return outer() == 6 ? 6 : 2;
}
