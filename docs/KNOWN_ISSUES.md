# Known Issues

## Materialization follow-up

- Namespace-scoped out-of-line explicit member specializations that rely on a
  defaulted class-template argument still appear to miss the final owner/member
  binding. A reduced namespaced variant of
  `tests/test_template_spec_outofline_default_arg_ret42.cpp` linked with an
  unresolved member symbol while the global form passed.
