with open("src/widgets/aroma_container.c", "r") as f:
    text = f.read()

# Replace EVENT_TYPE_TOUCH_DOWN with case EVENT_TYPE_MOUSE_CLICK: ... case EVENT_TYPE_TOUCH_DOWN:
import re
text = text.replace("    case EVENT_TYPE_TOUCH_DOWN:\n    {", 
"""    case EVENT_TYPE_MOUSE_CLICK:
    case EVENT_TYPE_TOUCH_DOWN:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_CLICK) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
""")
text = text.replace("        int tx = event->data.touch.x;\n        int ty = event->data.touch.y;", "")
text = text.replace("event->data.touch.id", "id")

text = text.replace("    case EVENT_TYPE_TOUCH_MOVE:\n    {", 
"""    case EVENT_TYPE_MOUSE_MOVE:
    case EVENT_TYPE_TOUCH_MOVE:
    {
        int tx, ty, id;
        if (event->event_type == EVENT_TYPE_MOUSE_MOVE) {
            tx = event->data.mouse.x; ty = event->data.mouse.y; id = 0;
        } else {
            tx = event->data.touch.x; ty = event->data.touch.y; id = event->data.touch.id;
        }
""")
text = text.replace("        int tx = event->data.touch.x;\n        int ty = event->data.touch.y;", "")
# There is a second place with EVENT_TYPE_MOUSE_MOVE previously. Where is it?
text = re.sub(r"    case EVENT_TYPE_MOUSE_MOVE:.*?(?=\n    case EVENT_TYPE_.*|default:)", "", text, flags=re.DOTALL)
# wait, my regex might match wrongly if there are other cases.

with open("src/widgets/aroma_container.c", "w") as f:
    f.write(text)
