# Foundation::Json - `foundation.json` (hand-rolled JSON DOM: value + parser + writer, UTF-8).
# The engine's JSON boundary codec: the MCP wire protocol + game-chosen data interchange. The value
# type is RTTI-reflected so scripts can parse/build/query/stringify JSON. Deliberately NO ISerializer
# backend - JSON is not an engine data format (engine data stays XML). Depends only on Core.

add_library(Json STATIC)
add_library(Foundation::Json ALIAS Json)

target_link_libraries(Json PUBLIC Foundation::Core Foundation::Policy)

# Reflection body (out-of-line impl unit): a regular source, NOT in the CXX_MODULES set.
target_sources(Json PRIVATE
    ReflectionImpl.cpp)   # REFLECT_VALUE(JsonValue) body (see Reflection.cppm)

target_sources(Json
    PUBLIC
        FILE_SET json_modules TYPE CXX_MODULES FILES
            Value.cppm
            Writer.cppm
            Parser.cppm
            Reflection.cppm
            JsonModule.cppm)
