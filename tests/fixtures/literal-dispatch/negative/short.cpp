// negative: a short, closed chain stays (ADR-014)
int sign(int x) {
  if (x > 0) return 1;
  else if (x < 0) return -1;
  else if (x == 0) return 0;
  return 0;
}
