// A local variable that shadows an outer declaration of the same spelling must
// get its own frame slot, and every reference must name the frame of the
// declaration that is visible at that point. The original defect collapsed both
// declarations onto one slot, so the inner object overwrote the still-live
// outer one.
//
// Each accepted shape contributes a distinct amount; the final comparison uses
// one literal so any single wrong read is visible as a nonzero exit code.

#include <cstdio>

struct Pair {
	int first;
	short second;
};

struct Counter {
	int value;
	int bump() {
		value += 3;
		return value;
	}
	int copiedBump() {
		auto twice = [*this]() mutable {
			value += 5;
			return value;
		};
		return twice() + twice() + value;
	}
};

int addThroughRef(int& target, int value) {
	target += value;
	return target;
}

int main() {
	int total = 0;

	// Sequential sibling blocks: the outer value must survive the inner block.
	{
		int sample = 1000;
		{
			int sample = 2000;
			total += sample;
		}
		total += sample;
	}

	// Nested repeated shadowing: only the innermost declaration is live.
	{
		int depth = 1;
		{
			int depth = 2;
			{
				int depth = 3;
				total += depth;
			}
			total += depth;
		}
		total += depth;
	}

	// Loop variables of sequential loops share a spelling.
	{
		int sum = 0;
		for (int index = 0; index < 3; ++index) {
			sum += index;
		}
		for (int index = 10; index < 13; ++index) {
			sum += index;
		}
		total += sum;
	}

	// Aggregate initialization, member stores, member reads and a receiver
	// address all have to agree on the shadowed object's frame.
	{
		int probe = 7;
		{
			Pair pair = {8, 9};
			total += pair.first + pair.second;
		}
		total += probe;
	}

	// Reference argument binding through a shadowed local.
	{
		int slot = 5;
		{
			int slot = 40;
			addThroughRef(slot, 2);
			total += slot;
		}
		total += slot;
	}

	// Member function calls, virtual-free, through a shadowed receiver, plus a
	// copied-this lambda whose body mutates its own copy.
	{
		Counter counter = {1};
		{
			Counter counter = {10};
			total += counter.copiedBump();
		}
		total += counter.value;
	}

	// Catch parameters of sibling handlers may share a spelling.
	{
		int caught = 0;
		try {
			throw 11;
		} catch (int error) {
			caught += error;
		}
		try {
			throw 22;
		} catch (int error) {
			caught += error;
		}
		total += caught;
	}

	// The outer declaration must still be usable after all inner blocks.
	{
		int linger = 500;
		{
			int linger = 600;
			(void)linger;
		}
		total += linger;
	}

	const int expected = 3692;
	if (total != expected) {
		printf("total=%d expected=%d\n", total, expected);
		return 1;
	}
	return 0;
}