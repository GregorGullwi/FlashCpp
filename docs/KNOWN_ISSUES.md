# Known Issues

This file tracks currently open issues only. Fixed items are removed once they are
validated.

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
