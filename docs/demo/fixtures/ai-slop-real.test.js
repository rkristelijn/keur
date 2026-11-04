test('parses the config', () => {
  const result = parseConfig(raw);
  expect(result.port).toBe(8080);
});
