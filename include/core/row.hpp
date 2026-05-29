#pragma once
#include <vector>

#include "core/value.hpp"

namespace dbms {

struct Row {
  std::vector<Value> cells;
};

}  // namespace dbms
