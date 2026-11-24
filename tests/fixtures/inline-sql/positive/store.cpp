// positive: SQL embedded in C++ (spike S-3 shapes)
const char *kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS snapshot (id INTEGER PRIMARY KEY);
)SQL";
void a(sqlite3 *db) {
  sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
  sqlite3_prepare_v2(db, "INSERT INTO snapshot(provider, query) VALUES (?, ?)", -1, &st, nullptr);
  sqlite3_prepare_v2(db, "SELECT count(*) FROM entity", -1, &st, nullptr);
  sqlite3_prepare_v2(db, "UPDATE entity SET data_json = ? WHERE id = ?", -1, &st, nullptr);
  sqlite3_prepare_v2(db, "DELETE FROM entity WHERE id = ?", -1, &st, nullptr);
}
