import sys
import os

def convert_to_c(input_file, output_file, array_name):
    try:
        with open(input_file, 'rb') as f:
            data = f.read()
    except FileNotFoundError:
        print(f"Error: File {input_file} not found.")
        return

    c_content = f'#include "aroma_material_font.h"\n\n'
    c_content += '#ifdef __cplusplus\nextern "C" {\n#endif\n\n'
    c_content += f'unsigned int {array_name}_len = {len(data)};\n\n'
    c_content += f'unsigned char {array_name}[] = {{\n'

    for i, byte in enumerate(data):
        c_content += f'0x{byte:02x}, '
        if (i + 1) % 12 == 0:
            c_content += '\n'

    c_content += '};\n\n'
    c_content += '#ifdef __cplusplus\n}\n#endif\n'

    with open(output_file, 'w') as f:
        f.write(c_content)
    
    print(f"Successfully generated {output_file}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python ttf_to_c.py <input.ttf> [output.c]")
        sys.exit(1)

    input_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]
    else:
        output_path = "aroma_material_font.c"
        
    convert_to_c(input_path, output_path, "aroma_material_ttf")
