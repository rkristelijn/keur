// positive: endpoints, paths and query strings embedded in C++ (spike S-3 shapes)
constexpr const char *kBase = "https://api.clickup.com/api/v2";
auto u = http_get(std::string(kBase) + "/user", auth);
std::string url = base + "/team/" + team_id + "/task?assignees%5B%5D=" + uid;
curl_easy_setopt(curl, CURLOPT_URL, "https://example.org/rest/items");
