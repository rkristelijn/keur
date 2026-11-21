# negative: Python file paths and config-driven requests
root = "/usr/local/share/loods"
cfg = load("catalog/ticketing/clickup/provider.toml")
r = session.get(cfg.base_url + cfg.query("my-tasks").path)
