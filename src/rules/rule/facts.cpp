/**
 * @file rule/facts.cpp
 * @brief Facts derivation (ADR-009) — read direct manifest pins + file presence.
 * @implements ADR-009.facts
 *
 * Shallow by design: direct pins only, no transitive resolution. Readers are
 * pure (content -> facts); derive_facts() is the single I/O seam that walks the
 * tree and feeds them. Kept small — the engine gains a reader, not a catalogue.
 */
#include "rule/facts.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <fstream>
#include <sstream>

namespace keur {

namespace {

std::string trim_ws(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

// Strip surrounding quotes and a leading version operator (^ ~ >= <= = v).
std::string clean_version(std::string v) {
  v = trim_ws(v);
  if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') && v.back() == v.front())
    v = v.substr(1, v.size() - 2);
  size_t i = 0;
  while (i < v.size() && (v[i] == '^' || v[i] == '~' || v[i] == '>' || v[i] == '<' ||
                          v[i] == '=' || v[i] == ' ' || v[i] == 'v'))
    i++;
  return v.substr(i);
}

// Iterate the "key": "value" pairs inside the object that follows `objkey`.
// Calls sink(name, value) for each. Stops at the object's closing brace.
template <typename Sink>
void json_object_pairs(const std::string& body, const std::string& objkey, Sink sink) {
  std::string needle = "\"" + objkey + "\"";
  size_t p = body.find(needle);
  if (p == std::string::npos) return;
  size_t open = body.find('{', p);
  if (open == std::string::npos) return;
  int depth = 0;
  size_t i = open;
  for (; i < body.size(); i++) {
    if (body[i] == '{') depth++;
    else if (body[i] == '}') { depth--; if (depth == 0) break; }
  }
  size_t close = i;
  size_t cur = open + 1;
  while (cur < close) {
    size_t ks = body.find('"', cur);
    if (ks == std::string::npos || ks >= close) break;
    size_t ke = body.find('"', ks + 1);
    if (ke == std::string::npos || ke >= close) break;
    std::string name = body.substr(ks + 1, ke - ks - 1);
    size_t colon = body.find(':', ke + 1);
    if (colon == std::string::npos || colon >= close) break;
    size_t vs = body.find('"', colon + 1);
    if (vs == std::string::npos || vs >= close) break;
    size_t ve = body.find('"', vs + 1);
    if (ve == std::string::npos || ve > close) break;
    std::string val = body.substr(vs + 1, ve - vs - 1);
    sink(name, val);
    cur = ve + 1;
  }
}

bool ends_with_(const std::string& s, const std::string& suf) {
  return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

}  // namespace

std::string facts_lang_for_ext(const std::string& ext) {
  if (ext == ".ts" || ext == ".tsx") return "ts";
  if (ext == ".js" || ext == ".jsx" || ext == ".mjs" || ext == ".cjs") return "js";
  if (ext == ".go") return "go";
  if (ext == ".py") return "python";
  if (ext == ".java") return "java";
  if (ext == ".rb") return "ruby";
  if (ext == ".php") return "php";
  if (ext == ".rs") return "rust";
  if (ext == ".c" || ext == ".h") return "c";
  if (ext == ".cpp" || ext == ".cc" || ext == ".hpp") return "cpp";
  return "";
}

void facts_read_package_json(const std::string& content, Facts& out) {
  // dependencies + devDependencies -> frameworks + direct version pins.
  auto add_dep = [&out](const std::string& name, const std::string& ver) {
    out.frameworks.insert(name);
    std::string v = clean_version(ver);
    if (!v.empty()) out.versions[name] = v;
  };
  json_object_pairs(content, "dependencies", add_dep);
  json_object_pairs(content, "devDependencies", add_dep);
  // engines.node / engines.npm -> runtimes.
  json_object_pairs(content, "engines", [&out](const std::string& name, const std::string& ver) {
    std::string v = clean_version(ver);
    if (!v.empty()) out.runtimes[name] = v;
  });
  // package.json implies a JS/TS project even before we see source files.
  out.langs.insert("js");
}

void facts_read_go_mod(const std::string& content, Facts& out) {
  out.langs.insert("go");
  std::istringstream in(content);
  std::string line;
  bool in_require_block = false;
  while (std::getline(in, line)) {
    std::string t = trim_ws(line);
    if (t.empty() || t[0] == '#') continue;
    if (t.rfind("go ", 0) == 0) {                 // `go 1.21`
      out.runtimes["go"] = clean_version(t.substr(3));
      continue;
    }
    if (t.rfind("require (", 0) == 0) { in_require_block = true; continue; }
    if (in_require_block && t == ")") { in_require_block = false; continue; }
    std::string dep;
    if (in_require_block) dep = t;                // `path v1.2.3`
    else if (t.rfind("require ", 0) == 0) dep = t.substr(8);  // single-line require
    if (dep.empty()) continue;
    std::istringstream ls(dep);
    std::string path, ver;
    ls >> path >> ver;
    if (path.empty()) continue;
    out.frameworks.insert(path);
    std::string v = clean_version(ver);
    if (!v.empty()) out.versions[path] = v;
  }
}

void facts_read_dockerfile(const std::string& content, Facts& out) {
  out.frameworks.insert("docker");
  std::istringstream in(content);
  std::string line;
  while (std::getline(in, line)) {
    std::string t = trim_ws(line);
    // case-insensitive FROM
    if (t.size() < 5) continue;
    std::string head = t.substr(0, 4);
    for (auto& ch : head) ch = (char)std::tolower((unsigned char)ch);
    if (head != "from") continue;
    std::istringstream ls(t.substr(4));
    std::string image;
    ls >> image;                                  // e.g. node:18-alpine
    if (image.empty()) continue;
    size_t colon = image.find(':');
    std::string name = colon == std::string::npos ? image : image.substr(0, colon);
    std::string tag = colon == std::string::npos ? "" : image.substr(colon + 1);
    // strip a registry/org prefix for the runtime name (library/node -> node).
    size_t slash = name.rfind('/');
    if (slash != std::string::npos) name = name.substr(slash + 1);
    if (!name.empty() && !tag.empty()) out.runtimes[name] = tag;
  }
}

void facts_read_pom_xml(const std::string& content, Facts& out) {
  out.langs.insert("java");
  out.frameworks.insert("maven");
  // <java.version> or <maven.compiler.source> -> runtimes[java]
  for (const char* tag : {"<java.version>", "<maven.compiler.source>"}) {
    size_t p = content.find(tag);
    if (p == std::string::npos) continue;
    size_t s = p + std::string(tag).size();
    size_t e = content.find('<', s);
    if (e != std::string::npos) {
      std::string v = clean_version(content.substr(s, e - s));
      if (!v.empty()) { out.runtimes["java"] = v; break; }
    }
  }
  // <artifactId>NAME</artifactId> optionally followed by <version>V</version>.
  size_t pos = 0;
  const std::string aopen = "<artifactId>", aclose = "</artifactId>";
  while ((pos = content.find(aopen, pos)) != std::string::npos) {
    size_t s = pos + aopen.size();
    size_t e = content.find(aclose, s);
    if (e == std::string::npos) break;
    std::string art = trim_ws(content.substr(s, e - s));
    if (!art.empty()) out.frameworks.insert(art);
    // look ahead a short window for a sibling <version>
    size_t vp = content.find("<version>", e);
    if (vp != std::string::npos && vp - e < 200) {
      size_t vs = vp + 9;
      size_t ve = content.find("</version>", vs);
      if (ve != std::string::npos) {
        std::string v = clean_version(content.substr(vs, ve - vs));
        if (!v.empty() && !art.empty()) out.versions[art] = v;
      }
    }
    pos = e + aclose.size();
  }
}

void facts_read_composer_json(const std::string& content, Facts& out) {
  out.langs.insert("php");
  out.frameworks.insert("composer");
  auto add = [&out](const std::string& name, const std::string& ver) {
    if (name == "php") { std::string v = clean_version(ver); if (!v.empty()) out.runtimes["php"] = v; return; }
    out.frameworks.insert(name);
    std::string v = clean_version(ver);
    if (!v.empty()) out.versions[name] = v;
  };
  json_object_pairs(content, "require", add);
  json_object_pairs(content, "require-dev", add);
}

namespace {
// Walk the tree once: collect extensions (langs) and feed known manifests to
// their readers. This is the single I/O seam (ADR-003) — readers stay pure.
void walk_facts(const std::string& dir, Facts& out, int depth) {
  if (depth > 6) return;  // manifests live near the root; don't descend forever
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* e;
  while ((e = readdir(d)) != nullptr) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    std::string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (name[0] == '.' || name == "node_modules" || name == "vendor" ||
          name == "build" || name == "dist" || name == "target")
        continue;
      walk_facts(full, out, depth + 1);
      continue;
    }
    if (!S_ISREG(st.st_mode)) continue;
    // language from extension
    size_t dot = name.rfind('.');
    if (dot != std::string::npos) {
      std::string lang = facts_lang_for_ext(name.substr(dot));
      if (!lang.empty()) out.langs.insert(lang);
    }
    // manifests -> readers
    auto slurp = [&full]() {
      std::ifstream f(full);
      return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    };
    if (name == "package.json") facts_read_package_json(slurp(), out);
    else if (name == "go.mod") facts_read_go_mod(slurp(), out);
    else if (name == "pom.xml") facts_read_pom_xml(slurp(), out);
    else if (name == "composer.json") facts_read_composer_json(slurp(), out);
    else if (name == "Dockerfile" || ends_with_(name, ".Dockerfile") ||
             name == "dockerfile") facts_read_dockerfile(slurp(), out);
  }
  closedir(d);
}
}  // namespace

Facts derive_facts(const std::string& root) {
  Facts f;
  walk_facts(root, f, 0);
  return f;
}

std::string facts_to_json(const Facts& f) {
  auto arr = [](const std::set<std::string>& s) {
    std::string o = "[";
    bool first = true;
    for (auto& v : s) { if (!first) o += ","; o += "\"" + v + "\""; first = false; }
    return o + "]";
  };
  auto obj = [](const std::map<std::string, std::string>& m) {
    std::string o = "{";
    bool first = true;
    for (auto& [k, v] : m) { if (!first) o += ","; o += "\"" + k + "\":\"" + v + "\""; first = false; }
    return o + "}";
  };
  return std::string("{\"langs\":") + arr(f.langs) +
         ",\"frameworks\":" + arr(f.frameworks) +
         ",\"versions\":" + obj(f.versions) +
         ",\"runtimes\":" + obj(f.runtimes) + "}";
}

}  // namespace keur
