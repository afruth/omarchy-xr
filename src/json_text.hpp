#pragma once
#include <json-c/json.h>
#include <string>

inline std::string jsonText(const std::string& value) {
    auto* object=json_object_new_string_len(value.data(), int(value.size()));
    const std::string text=json_object_to_json_string_ext(object, JSON_C_TO_STRING_PLAIN);
    json_object_put(object);
    return text;
}
