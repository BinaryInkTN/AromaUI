with open('/home/yassine/AromaUI/src/widgets/aroma_map.c', 'r') as f:
    text = f.read()

text = text.replace('aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __map_event_handler, NULL, 90);', 'aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_CLICK, __map_event_handler, NULL, 90);\n    aroma_event_subscribe(node->node_id, EVENT_TYPE_MOUSE_DOUBLE_CLICK, __map_event_handler, NULL, 90);')

with open('/home/yassine/AromaUI/src/widgets/aroma_map.c', 'w') as f:
    f.write(text)
