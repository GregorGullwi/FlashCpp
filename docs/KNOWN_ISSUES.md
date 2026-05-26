# Known Issues

- Namespace-qualified non-dependent calls in delayed template bodies can bind an
  overload declared after the template definition instead of preserving
  definition-time lookup. Example shape: `ns::choose<T>(0)` inside a function
  template where `choose(long)` is visible at definition and `choose(int)` is
  declared later. The unqualified equivalent is already definition-bound; the
  qualified replay path still needs parser/template metadata preservation.
