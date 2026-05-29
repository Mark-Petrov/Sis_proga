#include "core/persistence.hpp"

#include <filesystem>

namespace dbms {

void ensure_data_dir(const std::string &path) {
    std::filesystem::create_directories(path);
}

} // namespace dbms
