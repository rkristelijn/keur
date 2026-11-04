test('parses the config', () => {
  const result = parseConfig(raw);
  expect(result).toBeTruthy();
});
