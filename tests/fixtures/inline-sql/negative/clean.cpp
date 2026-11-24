// negative: SQL lives in a file; code only refers to it by name
#include "gen/sql.hpp"
// We SELECT rows FROM the store here (comment, not code).
void b(Store &s) { s.run(sql::insert_snapshot, args); }
const char *kVerb = "UPDATE";      // an enum-like token, not a statement
const char *kHelp = "select a task from the list where you left off";
