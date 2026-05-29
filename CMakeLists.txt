cmake_minimum_required(VERSION 3.14)
project(course_dbms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED)

add_library(dbms_core STATIC
  src/core/string_pool.cpp
  src/core/value.cpp
  src/core/row.cpp
  src/core/table.cpp
  src/core/database.cpp
  src/core/catalog.cpp
  src/core/btree.cpp
  src/core/journal.cpp
  src/core/persistence.cpp
  src/sql/lexer.cpp
  src/sql/parser.cpp
  src/sql/ast.cpp
  src/sql/where_eval.cpp
  src/exec/executor.cpp
  src/net/protocol.cpp
  src/auth/crypto.cpp
  src/auth/jwt.cpp
  src/auth/rbac.cpp
  src/server/job_queue.cpp
  src/server/telemetry.cpp
  src/server/access_log.cpp
)

target_include_directories(dbms_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(dbms_core PUBLIC OpenSSL::Crypto)

if(UNIX AND NOT APPLE)
  target_link_libraries(dbms_core PUBLIC pthread)
endif()

add_executable(prog src/client/main_client.cpp)
target_link_libraries(prog PRIVATE dbms_core)

add_executable(db_storage src/server/storage_main.cpp)
target_link_libraries(db_storage PRIVATE dbms_core)

add_executable(db_entry src/server/entrypoint_main.cpp)
target_link_libraries(db_entry PRIVATE dbms_core)
