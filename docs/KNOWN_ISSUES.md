# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

- `ConstructorVariant` default parameter values on name-mangling functions.
  Seven overloads in `src/NameMangling.h` (`generateItaniumMangledName`,
  `generateMangledName`, `generateMangledNameForConstructor`,
  `generateMangledNameFromNode`) use
  `ConstructorVariant constructor_variant = ConstructorVariant::Complete`.
  This lets callers silently omit the variant and get C1 (complete) mangling
  even when C2 (base-object) was intended, which would produce a wrong symbol
  at link time. The defaults should be removed and every call site should pass
  the variant explicitly. (PR #1176)
  
- C++20 range-for with an init-statement over a prvalue function-return container is
  still miscompiled. A minimal repro is
  `for (int factor = 2; auto value : makeContainer()) { sum += value * factor; }`,
  which currently produces the wrong runtime result even though plain
  `for (auto value : makeContainer())` works.

- Member access on a ternary object in address-of context can lose its struct
  `type_index` and fail with "struct type info not found for type_index=0".
  A minimal repro is:
  ```cpp
  struct Inner {
  	int value;
  };
  struct Outer {
  	Inner inner;
  };
  int main() {
  	Outer left;
  	Outer right;
  	left.inner.value = 10;
  	right.inner.value = 42;
  	bool pick_left = false;
  	int* ptr = &((pick_left ? left : right).inner.value);
  	if (*ptr != 42)
  		return 1;
  	*ptr = 99;
  	if (right.inner.value != 99)
  		return 2;
  	if (left.inner.value != 10)
  		return 3;
  	return 0;
  }
  ```

- Dependent pointer-to-template-parameter args (`Wrapper<T*>::tag`) accessed from
  inside another template are not yet handled correctly. A minimal repro is:
  ```cpp
  template <typename T>
  struct Wrapper {
      static constexpr int tag = sizeof(T);
  };

  template <typename T>
  struct Outer {
      static int getTag() { return Wrapper<T>::tag; }
      static int getPointerTag() { return Wrapper<T*>::tag; }
  };

  int main() {
      int result = 0;
      if (Outer<int>::getTag() != (int)sizeof(int)) result |= 1;
      if (Outer<double>::getTag() != (int)sizeof(double)) result |= 2;
      if (Wrapper<int>::tag != (int)sizeof(int)) result |= 4;
      if (Wrapper<double>::tag != (int)sizeof(double)) result |= 8;
      if (Outer<int>::getPointerTag() != (int)sizeof(int*)) result |= 16;
      return result;
  }
  ```
