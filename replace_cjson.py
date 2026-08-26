import re
with open('src/core/aroma_3d.c', 'r') as f:
    content = f.read()

# Replace types
content = content.replace('const JsonValue *', 'cJSON *')
content = content.replace('JsonValue *', 'cJSON *')

# Function calls
content = content.replace('json_object_get', 'cJSON_GetObjectItemCaseSensitive')
content = content.replace('json_array_get', 'cJSON_GetArrayItem')
content = content.replace('json_array_size', 'cJSON_GetArraySize')

# Type checks
content = re.sub(r'([A-Za-z0-9_]+(?:->[A-Za-z0-9_]+)*)->type == JSON_VAL_ARRAY', r'cJSON_IsArray(\1)', content)
content = re.sub(r'([A-Za-z0-9_]+(?:->[A-Za-z0-9_]+)*)->type == JSON_VAL_OBJECT', r'cJSON_IsObject(\1)', content)
content = re.sub(r'([A-Za-z0-9_]+(?:->[A-Za-z0-9_]+)*)->type == JSON_VAL_NUMBER', r'cJSON_IsNumber(\1)', content)
content = re.sub(r'([A-Za-z0-9_]+(?:->[A-Za-z0-9_]+)*)->type == JSON_VAL_STRING', r'cJSON_IsString(\1)', content)

# Field accesses
content = content.replace('->as.number', '->valuedouble')
content = content.replace('->as.string', '->valuestring')

# Array access
content = re.sub(r'([A-Za-z0-9_]+)->as\.array\.items\[([0-9]+)\]', r'cJSON_GetArrayItem(\1, \2)', content)

# Array size check
content = re.sub(r'([A-Za-z0-9_]+)->as\.array\.count', r'cJSON_GetArraySize(\1)', content)

# Parse function
parse_func = """static bool gltf_parse_document(const char *json_text, GLTFDocument **out_doc) {
    cJSON *root = cJSON_Parse(json_text);
    if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return false; }
    bool ok = gltf_parse_document_from_root(root, out_doc);
    cJSON_Delete(root);
    return ok;
}"""
content = re.sub(r'static bool gltf_parse_document\(const char \*json_text, GLTFDocument \*\*out_doc\) \{[\s\S]*?\}', parse_func, content)

with open('src/core/aroma_3d.c', 'w') as f:
    f.write(content)
