// positive: 8 else-if branches on string literals (KEUR-QUAL-001 and 001b)
void set(Rule& r, const std::string& key, const std::string& v) {
  if (key == "id") r.id = v;
  else if (key == "title") r.title = v;
  else if (key == "kind") r.kind = v;
  else if (key == "state") r.state = v;
  else if (key == "owner") r.owner = v;
  else if (key == "due") r.due = v;
  else if (key == "tags") r.tags = v;
  else if (key == "url") r.url = v;
  else if (key == "rank") r.rank = v;
}
