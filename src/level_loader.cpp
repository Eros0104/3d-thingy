#include "level_loader.hpp"

#include "json_level.hpp"
#include "level_binary.hpp"

namespace engine {

bool load_level_any(const char *path, Level &out, std::string &err) {
  const std::string p = path;
  if (p.size() >= 5 && p.compare(p.size() - 5, 5, ".evil") == 0) {
    return load_level_binary(path, out, err);
  }
  return load_json_level(path, out, err);
}

} // namespace engine
