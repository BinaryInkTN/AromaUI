#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "../../src/core/cJSON.c"

int main() {
    FILE *f = fopen("assets/damaged_helmet.glb", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = malloc(size);
    fread(data, 1, size, f);
    fclose(f);

    uint32_t total = (uint32_t)data[8] | ((uint32_t)data[9] << 8) | ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    size_t offset = 12;
    const unsigned char *json_text = NULL;
    while (offset + 8 <= size && offset + 8 <= total) {
        uint32_t chunk_len = (uint32_t)data[offset] | ((uint32_t)data[offset + 1] << 8) | ((uint32_t)data[offset + 2] << 16) | ((uint32_t)data[offset + 3] << 24);
        uint32_t chunk_type = (uint32_t)data[offset + 4] | ((uint32_t)data[offset + 5] << 8) | ((uint32_t)data[offset + 6] << 16) | ((uint32_t)data[offset + 7] << 24);
        if (chunk_type == 0x4E4F534A) {
            unsigned char *json_buf = malloc(chunk_len + 1);
            memcpy(json_buf, data + offset + 8, chunk_len);
            json_buf[chunk_len] = '\0';
            json_text = json_buf;
            break;
        }
        offset += 8 + chunk_len;
    }
    
    if (json_text) {
        cJSON *root = cJSON_Parse((const char *)json_text);
        if (root) {
            cJSON *meshes = cJSON_GetObjectItemCaseSensitive(root, "meshes");
            if (meshes) {
                cJSON *mesh = cJSON_GetArrayItem(meshes, 0);
                cJSON *prims = cJSON_GetObjectItemCaseSensitive(mesh, "primitives");
                cJSON *prim = cJSON_GetArrayItem(prims, 0);
                cJSON *indices = cJSON_GetObjectItemCaseSensitive(prim, "indices");
                cJSON *attrs = cJSON_GetObjectItemCaseSensitive(prim, "attributes");
                printf("indices accessor: %d\n", indices ? (int)indices->valuedouble : -1);
                cJSON *pos = cJSON_GetObjectItemCaseSensitive(attrs, "POSITION");
                printf("pos accessor: %d\n", pos ? (int)pos->valuedouble : -1);
            }
        }
    }
    return 0;
}
