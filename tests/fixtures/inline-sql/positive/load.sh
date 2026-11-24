#!/bin/sh
# positive: shell calling sqlite3 with inline SQL
sqlite3 "$DB" "SELECT name FROM sqlite_master WHERE type='table';"
sqlite3 "$DB" <<'SQL'
CREATE INDEX idx_a ON t(a);
SQL
