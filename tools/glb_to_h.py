#!/usr/bin/env python3
"""Embed a .glb 3D model as a C header for AromaUI live previews/tests.

Usage:
    python3 tools/glb_to_h.py <model.glb> <array_name> <output.h>

Example:
    python3 tools/glb_to_h.py Fox.glb aroma_fox_glb src/core/aroma_fox_model.h

The output declares `static const unsigned char <array_name>[]` and
`static const unsigned int <array_name>_len`, so including it in exactly
one .c file keeps the bytes in read-only data with no link conflicts.
"""
import sys


def convert_to_h(input_file, array_name, output_file):
    try:
        with open(input_file, "rb") as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File {input_file} not found.")
        return 1

    lines = [
        "/*",
        f" * Embedded 3D model generated from {input_file} ({len(data)} bytes).",
        " * Regenerate: python3 tools/glb_to_h.py "
        f"{input_file} {array_name} {output_file}",
        " */",
        "#ifndef AROMA_EMBEDDED_MODEL_H",
        "#define AROMA_EMBEDDED_MODEL_H",
        "",
        "#include <stddef.h>",
        "",
        "#ifdef __cplusplus",
        'extern "C" {',
        "#endif",
        "",
        f"static const unsigned int {array_name}_len = {len(data)};",
        "",
        f"static const unsigned char {array_name}[] = {{",
    ]
    row = []
    for b in data:
        row.append(f"0x{b:02x}")
        if len(row) == 12:
            lines.append(", ".join(row) + ",")
            row = []
    if row:
        lines.append(", ".join(row) + ",")
    lines += ["};", "", "#ifdef __cplusplus", "}", "#endif", "", "#endif"]

    with open(output_file, "w") as f:
        f.write("\n".join(lines) + "\n")

    print(f"Successfully generated {output_file} ({len(data)} bytes embedded)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print("Usage: python3 tools/glb_to_h.py <model.glb> <array_name> <output.h>")
        sys.exit(1)
    sys.exit(convert_to_h(sys.argv[1], sys.argv[2], sys.argv[3]))
