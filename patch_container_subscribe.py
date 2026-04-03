import re

with open("src/widgets/aroma_container.c", "r") as f:
    text = f.read()

pattern = r"(aroma_event_subscribe\(node->node_id, EVENT_TYPE_MOUSE_MOVE, scroll_event_handler, node, 0\);)"
text = re.sub(pattern, r"\1\n        aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_SCROLL, scroll_event_handler, node, 0);", text)

with open("src/widgets/aroma_container.c", "w") as f:
    f.write(text)

