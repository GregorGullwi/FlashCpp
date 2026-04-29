# Known Issues

- Ternary expressions that select between function-pointer prvalues can still
  miscompile at runtime on the Linux/clang path. A focused reproducer like
  `(true ? forward(get()) : get())(41)` currently truncates the selected
  function pointer during ternary lowering, leading to a crash when the
  indirect call executes. The sema-side callable typing fix landed, but the
  backend ternary codegen still needs a dedicated non-arithmetic common-type
  path for function pointers.
