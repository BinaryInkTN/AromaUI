import re

with open("include/aroma_event.h", "r") as f:
    text = f.read()

# Add EVENT_TYPE_MOUSE_SCROLL
pattern = r"(EVENT_TYPE_MOUSE_DOUBLE_CLICK,\s*/\*\*< Double click detected\. \*/)"
replacement = r"\1\n    EVENT_TYPE_MOUSE_SCROLL,      /**< Mouse scroll event. */"
text = re.sub(pattern, replacement, text)

# Add scroll_x and scroll_y 
pattern_struct = r"(uint8_t clicks;\s*/\*\*< Click count \(e\.g\. 1=single, 2=double\)\. \*/\n\}) AromaMouseEventData;"
replacement_struct = r"    uint8_t clicks; /**< Click count (e.g. 1=single, 2=double). */\n    float scroll_x; /**< Horizontal scroll delta. */\n    float scroll_y; /**< Vertical scroll delta. */\n} AromaMouseEventData;"
text = re.sub(pattern_struct, replacement_struct, text, flags=re.DOTALL)

# Add aroma_event_create_scroll
pattern_func = r"(AromaEvent\* aroma_event_create_mouse\(AromaEventType event_type, uint64_t target_node_id,\n\s*int x, int y, uint8_t button\);)"
replacement_func = r"\1\n\nAromaEvent* aroma_event_create_scroll(uint64_t target_node_id, int x, int y, float scroll_x, float scroll_y);"
text = re.sub(pattern_func, replacement_func, text)

with open("include/aroma_event.h", "w") as f:
    f.write(text)

