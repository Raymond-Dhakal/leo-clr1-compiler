cmake_minimum_required(VERSION 3.16)
project(Leo_Compiler C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

include_directories(${CMAKE_SOURCE_DIR}/include)

# The CLR(1) generator core, shared by both executables.
set(GEN_SOURCES
    src/grammar.c src/lexer.c src/first.c src/lr1_item.c src/closure.c
    src/goto.c src/state.c src/canonical.c src/parsing_table.c
    src/conflict.c src/parser.c)

file(GLOB NOVA_SOURCES ${CMAKE_SOURCE_DIR}/src/compiler/*.c)

# leoc: the Leo compiler (the main product).
add_executable(leoc ${GEN_SOURCES} ${NOVA_SOURCES})
target_compile_definitions(leoc PRIVATE
    LEO_DEFAULT_GRAMMAR="${CMAKE_SOURCE_DIR}/language/grammar.txt")
target_link_libraries(leoc m)

# clr_parser: the original standalone parser generator tool.
add_executable(clr_parser ${GEN_SOURCES} src/main.c)

# Run the whole test suite:  cmake --build . --target check
add_custom_target(check
    COMMAND ${CMAKE_COMMAND} -E env LEOC=$<TARGET_FILE:leoc> CLR=$<TARGET_FILE:clr_parser>
            ${CMAKE_SOURCE_DIR}/tests/run_tests.sh
    DEPENDS leoc clr_parser
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
