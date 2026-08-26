import re
with open('src/core/aroma_3d.c', 'r') as f:
    lines = f.readlines()

start_idx = -1
end_idx = -1

for i, line in enumerate(lines):
    if "typedef enum { JSON_TOK_NULL" in line:
        start_idx = i
    if "json_parse trailing chars at pos=" in line:
        pass
    if "#define AROMA_3D_MAX_ATTRS" in line:
        end_idx = i
        break

if start_idx != -1 and end_idx != -1:
    # Delete from start_idx to end_idx - 1 (inclusive)
    new_lines = lines[:start_idx] + ['#include "cJSON.h"\n\n'] + lines[end_idx:]
    with open('src/core/aroma_3d.c', 'w') as f:
        f.writelines(new_lines)
