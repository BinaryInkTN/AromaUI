import re

with open("src/backends/platforms/aroma_platform_glps.c", "r") as f:
    text = f.read()

text = text.replace("aroma_event_create_scroll(0, -1, -1,", "aroma_event_create_scroll(0, (int)platform_ctx.last_mouse_x, (int)platform_ctx.last_mouse_y,")

with open("src/backends/platforms/aroma_platform_glps.c", "w") as f:
    f.write(text)

