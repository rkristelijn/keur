// negative: format-only, single words, values, comments ("print a nice message here")
void ok(int n, const char *s) {
  std::printf("%s\n", s);
  std::fprintf(stderr, "%s: %d\n", s, n);
  std::cout << value << "\n";
  std::cout << "ok\n";
  std::printf("--version\n");
  report(diag::cat_unknown_key, {{"key", k}});
}
