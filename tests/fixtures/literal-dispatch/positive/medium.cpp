// positive for 001b only: 4 else-if branches (level 5 asks for a table earlier)
void set(Rule& r, const std::string& key, const std::string& v) {
  if (key == "id") r.id = v;
  else if (key == "title") r.title = v;
  else if (key == "kind") r.kind = v;
  else if (key == "state") r.state = v;
  else if (key == "owner") r.owner = v;
}
