#include "core/database.hpp"

namespace dbms {

bool Database::create_table(const std::string &tname,
                            std::vector<ColumnDef> cols, SharedStringPool pool,
                            std::string &err) {
    if (tables_.count(tname)) {
        err = "table already exists: " + tname;
        return false;
    }
    tables_[tname] =
        std::make_shared<Table>(tname, std::move(cols), std::move(pool));
    return true;
}

bool Database::drop_table(const std::string &tname, std::string &err) {
    if (!tables_.count(tname)) {
        err = "table not found: " + tname;
        return false;
    }
    tables_.erase(tname);
    return true;
}

Table *Database::get_table(const std::string &tname) {
    auto it = tables_.find(tname);
    return it == tables_.end() ? nullptr : it->second.get();
}

const Table *Database::get_table(const std::string &tname) const {
    auto it = tables_.find(tname);
    return it == tables_.end() ? nullptr : it->second.get();
}

} // namespace dbms
