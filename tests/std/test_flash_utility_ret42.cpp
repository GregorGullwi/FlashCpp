// Test flash_utility.h - Comprehensive utility functions test
// Expected return value: 42

#include "flash_minimal/flash_utility.h"

using namespace flash_std;

// Test move
int test_move() {
	int x = 42;
	int y = move(x);
	return y;  // Should be 42
}

// Test forward
template<typename T>
int test_forward_impl(T&& value) {
	return forward<T>(value);
}

int test_forward() {
	int x = 15;
	return test_forward_impl(x) + test_forward_impl(27);  // 15 + 27 = 42
}

// Test addressof
struct WithOperatorAddress {
	int value;
	
	// Overloaded operator&
	int* operator&() {
		return nullptr;  // Returns null to test that addressof bypasses this
	}
};

int test_addressof() {
	WithOperatorAddress obj;
	obj.value = 42;
	
	// addressof should bypass the overloaded operator&
	WithOperatorAddress* ptr = addressof(obj);
	return ptr->value;  // Should be 42
}

// Test swap
int test_swap() {
	int a = 10;
	int b = 32;
	swap(a, b);
	return a + b;  // 32 + 10 = 42
}

// Test pair
int test_pair() {
	pair<int, int> p1(10, 20);
	pair<int, int> p2 = make_pair(5, 7);
	
	int sum = p1.first + p1.second + p2.first + p2.second;
	// 10 + 20 + 5 + 7 = 42
	return sum;
}

// Test pair comparison
int test_pair_comparison() {
	pair<int, int> p1(10, 20);
	pair<int, int> p2(10, 20);
	pair<int, int> p3(10, 21);
	
	if (!(p1 == p2)) return 1;
	if (p1 != p2) return 2;
	if (p1 == p3) return 3;
	if (!(p1 != p3)) return 4;
	if (!(p1 < p3)) return 5;
	if (p3 < p1) return 6;
	
	return 42;
}

// Test pair swap
int test_pair_swap() {
	pair<int, int> p1(10, 20);
	pair<int, int> p2(5, 7);
	
	p1.swap(p2);
	
	// After swap: p1 = (5, 7), p2 = (10, 20)
	return p1.first + p1.second + p2.first + p2.second;
	// 5 + 7 + 10 + 20 = 42
}

// Test exchange
int test_exchange() {
	int x = 10;
	int old_x = exchange(x, 32);
	
	// old_x should be 10, x should be 32
	return old_x + x;  // 10 + 32 = 42
}

// Test as_const
int test_as_const() {
	int x = 42;
	const int& cx = as_const(x);
	return cx;  // Should be 42
}

// Test pair move
int test_pair_move() {
	pair<int, int> p1(10, 32);
	pair<int, int> p2 = move(p1);
	
	return p2.first + p2.second;  // 10 + 32 = 42
}

// Test pair assignment
int test_pair_assignment() {
	pair<int, int> p1(10, 20);
	pair<int, int> p2(5, 7);
	
	p1 = p2;
	
	return p1.first * p1.second + 7;  // 5 * 7 + 7 = 42
}

int main() {
	int result = 0;
	
	result = test_move();
	if (result != 42) return 1;
	
	result = test_forward();
	if (result != 42) return 2;
	
	result = test_addressof();
	if (result != 42) return 3;
	
	result = test_swap();
	if (result != 42) return 4;
	
	result = test_pair();
	if (result != 42) return 5;
	
	result = test_pair_comparison();
	if (result != 42) return 6;
	
	result = test_pair_swap();
	if (result != 42) return 7;
	
	result = test_exchange();
	if (result != 42) return 8;
	
	result = test_as_const();
	if (result != 42) return 9;
	
	result = test_pair_move();
	if (result != 42) return 10;
	
	result = test_pair_assignment();
	if (result != 42) return 11;
	
	// All tests passed
	return 42;
}
