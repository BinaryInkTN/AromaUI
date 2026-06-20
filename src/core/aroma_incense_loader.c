#include "aroma_incense_loader.h"
#include "aroma_incense.h"
#include "aroma_ui.h"
#include "aroma_material_icons.h"
#include "widgets/aroma_canvas.h"
#include "widgets/aroma_debug_overlay.h"
#include "widgets/aroma_dropdown.h"
#include "widgets/aroma_gif.h"
#include "widgets/aroma_icon.h"
#include "widgets/aroma_loading.h"
#include "widgets/aroma_map.h"
#include "widgets/aroma_menu.h"
#include "widgets/aroma_radiobutton.h"
#include "widgets/aroma_sidebar.h"
#include "widgets/aroma_table.h"
#include "widgets/aroma_tabs.h"
#include "widgets/aroma_tooltip.h"
#include "widgets/aroma_container.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_CALLBACKS 128
#define MAX_NAMED_WIDGETS 256
#define MAX_PROPS 64
#define MAX_CHILDREN 64
#define MAX_ITEM_NODES 64

typedef struct {
    char name[64];
    IncenseCallbackType type;
    void *fn;
    void *userdata;
} CallbackEntry;

typedef struct {
    char id[64];
    AromaNode *node;
} NamedWidget;

typedef struct {
    NamedWidget items[MAX_NAMED_WIDGETS];
    int count;
} WidgetRegistry;

typedef struct {
    const char *key;
    const char *value;
} Prop;

typedef struct {
    Prop items[MAX_PROPS];
    int count;
} PropBag;

typedef struct {
    WidgetRegistry *registry;
    AromaFont *font;
    AromaFont *icon_font;
} BuildCtx;

typedef AromaNode *(*WidgetBuilder)(IncenseNode *node, AromaNode *parent, BuildCtx *ctx);

typedef struct {
    const char *name;
    WidgetBuilder build;
} WidgetEntry;

typedef struct {
    const char *name;
    const char *codepoint;
} IconMapping;

static const IconMapping ICON_MAP[] = {
    {"AROMA_ICON_3D_ROTATION", "\ue84d"},
    {"AROMA_ICON_AC_UNIT", "\ueb3b"},
    {"AROMA_ICON_ACCESS_ALARM", "\ue190"},
    {"AROMA_ICON_ACCESS_ALARMS", "\ue191"},
    {"AROMA_ICON_ACCESS_TIME", "\ue192"},
    {"AROMA_ICON_ACCESSIBILITY", "\ue84e"},
    {"AROMA_ICON_ACCESSIBLE", "\ue914"},
    {"AROMA_ICON_ACCOUNT_BALANCE", "\ue84f"},
    {"AROMA_ICON_ACCOUNT_BALANCE_WALLET", "\ue850"},
    {"AROMA_ICON_ACCOUNT_BOX", "\ue851"},
    {"AROMA_ICON_ACCOUNT_CIRCLE", "\ue853"},
    {"AROMA_ICON_ADB", "\ue60e"},
    {"AROMA_ICON_ADD", "\ue145"},
    {"AROMA_ICON_ADD_A_PHOTO", "\ue439"},
    {"AROMA_ICON_ADD_ALARM", "\ue193"},
    {"AROMA_ICON_ADD_ALERT", "\ue003"},
    {"AROMA_ICON_ADD_BOX", "\ue146"},
    {"AROMA_ICON_ADD_CIRCLE", "\ue147"},
    {"AROMA_ICON_ADD_CIRCLE_OUTLINE", "\ue148"},
    {"AROMA_ICON_ADD_LOCATION", "\ue567"},
    {"AROMA_ICON_ADD_SHOPPING_CART", "\ue854"},
    {"AROMA_ICON_ADD_TO_PHOTOS", "\ue39d"},
    {"AROMA_ICON_ADD_TO_QUEUE", "\ue05c"},
    {"AROMA_ICON_ADJUST", "\ue39e"},
    {"AROMA_ICON_AIRLINE_SEAT_FLAT", "\ue630"},
    {"AROMA_ICON_AIRLINE_SEAT_FLAT_ANGLED", "\ue631"},
    {"AROMA_ICON_AIRLINE_SEAT_INDIVIDUAL_SUITE", "\ue632"},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_EXTRA", "\ue633"},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_NORMAL", "\ue634"},
    {"AROMA_ICON_AIRLINE_SEAT_LEGROOM_REDUCED", "\ue635"},
    {"AROMA_ICON_AIRLINE_SEAT_RECLINE_EXTRA", "\ue636"},
    {"AROMA_ICON_AIRLINE_SEAT_RECLINE_NORMAL", "\ue637"},
    {"AROMA_ICON_AIRPLANEMODE_ACTIVE", "\ue195"},
    {"AROMA_ICON_AIRPLANEMODE_INACTIVE", "\ue194"},
    {"AROMA_ICON_AIRPLAY", "\ue055"},
    {"AROMA_ICON_AIRPORT_SHUTTLE", "\ueb3c"},
    {"AROMA_ICON_ALARM", "\ue855"},
    {"AROMA_ICON_ALARM_ADD", "\ue856"},
    {"AROMA_ICON_ALARM_OFF", "\ue857"},
    {"AROMA_ICON_ALARM_ON", "\ue858"},
    {"AROMA_ICON_ALBUM", "\ue019"},
    {"AROMA_ICON_ALL_INCLUSIVE", "\ueb3d"},
    {"AROMA_ICON_ALL_OUT", "\ue90b"},
    {"AROMA_ICON_ANDROID", "\ue859"},
    {"AROMA_ICON_ANNOUNCEMENT", "\ue85a"},
    {"AROMA_ICON_APPS", "\ue5c3"},
    {"AROMA_ICON_ARCHIVE", "\ue149"},
    {"AROMA_ICON_ARROW_BACK", "\ue5c4"},
    {"AROMA_ICON_ARROW_BACK_IOS", "\ue5e0"},
    {"AROMA_ICON_ARROW_DOWNWARD", "\ue5db"},
    {"AROMA_ICON_ARROW_DROP_DOWN", "\ue5c5"},
    {"AROMA_ICON_ARROW_DROP_DOWN_CIRCLE", "\ue5c6"},
    {"AROMA_ICON_ARROW_DROP_UP", "\ue5c7"},
    {"AROMA_ICON_ARROW_FORWARD", "\ue5c8"},
    {"AROMA_ICON_ARROW_FORWARD_IOS", "\ue5e1"},
    {"AROMA_ICON_ARROW_LEFT", "\ue5de"},
    {"AROMA_ICON_ARROW_RIGHT", "\ue5df"},
    {"AROMA_ICON_ARROW_UPWARD", "\ue5d8"},
    {"AROMA_ICON_ART_TRACK", "\ue060"},
    {"AROMA_ICON_ASPECT_RATIO", "\ue85b"},
    {"AROMA_ICON_ASSESSMENT", "\ue85c"},
    {"AROMA_ICON_ASSIGNMENT", "\ue85d"},
    {"AROMA_ICON_ASSIGNMENT_IND", "\ue85e"},
    {"AROMA_ICON_ASSIGNMENT_LATE", "\ue85f"},
    {"AROMA_ICON_ASSIGNMENT_RETURN", "\ue860"},
    {"AROMA_ICON_ASSIGNMENT_RETURNED", "\ue861"},
    {"AROMA_ICON_ASSIGNMENT_TURNED_IN", "\ue862"},
    {"AROMA_ICON_ASSISTANT", "\ue39f"},
    {"AROMA_ICON_ASSISTANT_PHOTO", "\ue3a0"},
    {"AROMA_ICON_ATTACH_FILE", "\ue226"},
    {"AROMA_ICON_ATTACH_MONEY", "\ue227"},
    {"AROMA_ICON_ATTACHMENT", "\ue2bc"},
    {"AROMA_ICON_AUDIOTRACK", "\ue3a1"},
    {"AROMA_ICON_AUTORENEW", "\ue863"},
    {"AROMA_ICON_AV_TIMER", "\ue01b"},
    {"AROMA_ICON_BACKSPACE", "\ue14a"},
    {"AROMA_ICON_BACKUP", "\ue864"},
    {"AROMA_ICON_BATTERY_ALERT", "\ue19c"},
    {"AROMA_ICON_BATTERY_CHARGING_FULL", "\ue1a3"},
    {"AROMA_ICON_BATTERY_FULL", "\ue1a4"},
    {"AROMA_ICON_BATTERY_STD", "\ue1a5"},
    {"AROMA_ICON_BATTERY_UNKNOWN", "\ue1a6"},
    {"AROMA_ICON_BEACH_ACCESS", "\ueb3e"},
    {"AROMA_ICON_BEENHERE", "\ue52d"},
    {"AROMA_ICON_BLOCK", "\ue14b"},
    {"AROMA_ICON_BLUETOOTH", "\ue1a7"},
    {"AROMA_ICON_BLUETOOTH_AUDIO", "\ue60f"},
    {"AROMA_ICON_BLUETOOTH_CONNECTED", "\ue1a8"},
    {"AROMA_ICON_BLUETOOTH_DISABLED", "\ue1a9"},
    {"AROMA_ICON_BLUETOOTH_SEARCHING", "\ue1aa"},
    {"AROMA_ICON_BLUR_CIRCULAR", "\ue3a2"},
    {"AROMA_ICON_BLUR_LINEAR", "\ue3a3"},
    {"AROMA_ICON_BLUR_OFF", "\ue3a4"},
    {"AROMA_ICON_BLUR_ON", "\ue3a5"},
    {"AROMA_ICON_BOOK", "\ue865"},
    {"AROMA_ICON_BOOKMARK", "\ue866"},
    {"AROMA_ICON_BOOKMARK_BORDER", "\ue867"},
    {"AROMA_ICON_BORDER_ALL", "\ue228"},
    {"AROMA_ICON_BORDER_BOTTOM", "\ue229"},
    {"AROMA_ICON_BORDER_CLEAR", "\ue22a"},
    {"AROMA_ICON_BORDER_COLOR", "\ue22b"},
    {"AROMA_ICON_BORDER_HORIZONTAL", "\ue22c"},
    {"AROMA_ICON_BORDER_INNER", "\ue22d"},
    {"AROMA_ICON_BORDER_LEFT", "\ue22e"},
    {"AROMA_ICON_BORDER_OUTER", "\ue22f"},
    {"AROMA_ICON_BORDER_RIGHT", "\ue230"},
    {"AROMA_ICON_BORDER_STYLE", "\ue231"},
    {"AROMA_ICON_BORDER_TOP", "\ue232"},
    {"AROMA_ICON_BORDER_VERTICAL", "\ue233"},
    {"AROMA_ICON_BRANDING_WATERMARK", "\ue06b"},
    {"AROMA_ICON_BRIGHTNESS_1", "\ue3a6"},
    {"AROMA_ICON_BRIGHTNESS_2", "\ue3a7"},
    {"AROMA_ICON_BRIGHTNESS_3", "\ue3a8"},
    {"AROMA_ICON_BRIGHTNESS_4", "\ue3a9"},
    {"AROMA_ICON_BRIGHTNESS_5", "\ue3aa"},
    {"AROMA_ICON_BRIGHTNESS_6", "\ue3ab"},
    {"AROMA_ICON_BRIGHTNESS_7", "\ue3ac"},
    {"AROMA_ICON_BRIGHTNESS_AUTO", "\ue1ab"},
    {"AROMA_ICON_BRIGHTNESS_HIGH", "\ue1ac"},
    {"AROMA_ICON_BRIGHTNESS_LOW", "\ue1ad"},
    {"AROMA_ICON_BRIGHTNESS_MEDIUM", "\ue1ae"},
    {"AROMA_ICON_BROKEN_IMAGE", "\ue3ad"},
    {"AROMA_ICON_BRUSH", "\ue3ae"},
    {"AROMA_ICON_BUBBLE_CHART", "\ue6dd"},
    {"AROMA_ICON_BUG_REPORT", "\ue868"},
    {"AROMA_ICON_BUILD", "\ue869"},
    {"AROMA_ICON_BURST_MODE", "\ue43c"},
    {"AROMA_ICON_BUSINESS", "\ue0af"},
    {"AROMA_ICON_BUSINESS_CENTER", "\ueb3f"},
    {"AROMA_ICON_CACHED", "\ue86a"},
    {"AROMA_ICON_CAKE", "\ue7e9"},
    {"AROMA_ICON_CALL", "\ue0b0"},
    {"AROMA_ICON_CALL_END", "\ue0b1"},
    {"AROMA_ICON_CALL_MADE", "\ue0b2"},
    {"AROMA_ICON_CALL_MERGE", "\ue0b3"},
    {"AROMA_ICON_CALL_MISSED", "\ue0b4"},
    {"AROMA_ICON_CALL_MISSED_OUTGOING", "\ue0e4"},
    {"AROMA_ICON_CALL_RECEIVED", "\ue0b5"},
    {"AROMA_ICON_CALL_SPLIT", "\ue0b6"},
    {"AROMA_ICON_CALL_TO_ACTION", "\ue06c"},
    {"AROMA_ICON_CAMERA", "\ue3af"},
    {"AROMA_ICON_CAMERA_ALT", "\ue3b0"},
    {"AROMA_ICON_CAMERA_ENHANCE", "\ue8fc"},
    {"AROMA_ICON_CAMERA_FRONT", "\ue3b1"},
    {"AROMA_ICON_CAMERA_REAR", "\ue3b2"},
    {"AROMA_ICON_CAMERA_ROLL", "\ue3b3"},
    {"AROMA_ICON_CANCEL", "\ue5c9"},
    {"AROMA_ICON_CARD_GIFTCARD", "\ue8f6"},
    {"AROMA_ICON_CARD_MEMBERSHIP", "\ue8f7"},
    {"AROMA_ICON_CARD_TRAVEL", "\ue8f8"},
    {"AROMA_ICON_CASINO", "\ueb40"},
    {"AROMA_ICON_CAST", "\ue307"},
    {"AROMA_ICON_CAST_CONNECTED", "\ue308"},
    {"AROMA_ICON_CENTER_FOCUS_STRONG", "\ue3b4"},
    {"AROMA_ICON_CENTER_FOCUS_WEAK", "\ue3b5"},
    {"AROMA_ICON_CHANGE_HISTORY", "\ue86b"},
    {"AROMA_ICON_CHAT", "\ue0b7"},
    {"AROMA_ICON_CHAT_BUBBLE", "\ue0ca"},
    {"AROMA_ICON_CHAT_BUBBLE_OUTLINE", "\ue0cb"},
    {"AROMA_ICON_CHECK", "\ue5ca"},
    {"AROMA_ICON_CHECK_BOX", "\ue834"},
    {"AROMA_ICON_CHECK_BOX_OUTLINE_BLANK", "\ue835"},
    {"AROMA_ICON_CHECK_CIRCLE", "\ue86c"},
    {"AROMA_ICON_CHEVRON_LEFT", "\ue5cb"},
    {"AROMA_ICON_CHEVRON_RIGHT", "\ue5cc"},
    {"AROMA_ICON_CHILD_CARE", "\ueb41"},
    {"AROMA_ICON_CHILD_FRIENDLY", "\ueb42"},
    {"AROMA_ICON_CHROME_READER_MODE", "\ue86d"},
    {"AROMA_ICON_CLASS", "\ue86e"},
    {"AROMA_ICON_CLEAR", "\ue14c"},
    {"AROMA_ICON_CLEAR_ALL", "\ue0b8"},
    {"AROMA_ICON_CLOSE", "\ue5cd"},
    {"AROMA_ICON_CLOSED_CAPTION", "\ue01c"},
    {"AROMA_ICON_CLOUD", "\ue2bd"},
    {"AROMA_ICON_CLOUD_CIRCLE", "\ue2be"},
    {"AROMA_ICON_CLOUD_DONE", "\ue2bf"},
    {"AROMA_ICON_CLOUD_DOWNLOAD", "\ue2c0"},
    {"AROMA_ICON_CLOUD_OFF", "\ue2c1"},
    {"AROMA_ICON_CLOUD_QUEUE", "\ue2c2"},
    {"AROMA_ICON_CLOUD_UPLOAD", "\ue2c3"},
    {"AROMA_ICON_CODE", "\ue86f"},
    {"AROMA_ICON_COLLECTIONS", "\ue3b6"},
    {"AROMA_ICON_COLLECTIONS_BOOKMARK", "\ue431"},
    {"AROMA_ICON_COLOR_LENS", "\ue3b7"},
    {"AROMA_ICON_COLORIZE", "\ue3b8"},
    {"AROMA_ICON_COMMENT", "\ue0b9"},
    {"AROMA_ICON_COMPARE", "\ue3b9"},
    {"AROMA_ICON_COMPARE_ARROWS", "\ue915"},
    {"AROMA_ICON_COMPUTER", "\ue30a"},
    {"AROMA_ICON_CONFIRMATION_NUMBER", "\ue638"},
    {"AROMA_ICON_CONTACT_MAIL", "\ue0d0"},
    {"AROMA_ICON_CONTACT_PHONE", "\ue0cf"},
    {"AROMA_ICON_CONTACTS", "\ue0ba"},
    {"AROMA_ICON_CONTENT_COPY", "\ue14d"},
    {"AROMA_ICON_CONTENT_CUT", "\ue14e"},
    {"AROMA_ICON_CONTENT_PASTE", "\ue14f"},
    {"AROMA_ICON_CONTROL_POINT", "\ue3ba"},
    {"AROMA_ICON_CONTROL_POINT_DUPLICATE", "\ue3bb"},
    {"AROMA_ICON_COPYRIGHT", "\ue90c"},
    {"AROMA_ICON_CREATE", "\ue150"},
    {"AROMA_ICON_CREATE_NEW_FOLDER", "\ue2cc"},
    {"AROMA_ICON_CREDIT_CARD", "\ue870"},
    {"AROMA_ICON_CROP", "\ue3be"},
    {"AROMA_ICON_CROP_16_9", "\ue3bc"},
    {"AROMA_ICON_CROP_3_2", "\ue3bd"},
    {"AROMA_ICON_CROP_5_4", "\ue3bf"},
    {"AROMA_ICON_CROP_7_5", "\ue3c0"},
    {"AROMA_ICON_CROP_DIN", "\ue3c1"},
    {"AROMA_ICON_CROP_FREE", "\ue3c2"},
    {"AROMA_ICON_CROP_LANDSCAPE", "\ue3c3"},
    {"AROMA_ICON_CROP_ORIGINAL", "\ue3c4"},
    {"AROMA_ICON_CROP_PORTRAIT", "\ue3c5"},
    {"AROMA_ICON_CROP_ROTATE", "\ue437"},
    {"AROMA_ICON_CROP_SQUARE", "\ue3c6"},
    {"AROMA_ICON_DASHBOARD", "\ue871"},
    {"AROMA_ICON_DATA_USAGE", "\ue1af"},
    {"AROMA_ICON_DATE_RANGE", "\ue916"},
    {"AROMA_ICON_DEHAZE", "\ue3c7"},
    {"AROMA_ICON_DELETE", "\ue872"},
    {"AROMA_ICON_DELETE_FOREVER", "\ue92b"},
    {"AROMA_ICON_DELETE_SWEEP", "\ue16c"},
    {"AROMA_ICON_DESCRIPTION", "\ue873"},
    {"AROMA_ICON_DESKTOP_MAC", "\ue30b"},
    {"AROMA_ICON_DESKTOP_WINDOWS", "\ue30c"},
    {"AROMA_ICON_DETAILS", "\ue3c8"},
    {"AROMA_ICON_DEVELOPER_BOARD", "\ue30d"},
    {"AROMA_ICON_DEVELOPER_MODE", "\ue1b0"},
    {"AROMA_ICON_DEVICE_HUB", "\ue335"},
    {"AROMA_ICON_DEVICES", "\ue1b1"},
    {"AROMA_ICON_DEVICES_OTHER", "\ue337"},
    {"AROMA_ICON_DIALER_SIP", "\ue0bb"},
    {"AROMA_ICON_DIALPAD", "\ue0bc"},
    {"AROMA_ICON_DIRECTIONS", "\ue52e"},
    {"AROMA_ICON_DIRECTIONS_BIKE", "\ue52f"},
    {"AROMA_ICON_DIRECTIONS_BOAT", "\ue532"},
    {"AROMA_ICON_DIRECTIONS_BUS", "\ue530"},
    {"AROMA_ICON_DIRECTIONS_CAR", "\ue531"},
    {"AROMA_ICON_DIRECTIONS_RAILWAY", "\ue534"},
    {"AROMA_ICON_DIRECTIONS_RUN", "\ue566"},
    {"AROMA_ICON_DIRECTIONS_SUBWAY", "\ue533"},
    {"AROMA_ICON_DIRECTIONS_TRANSIT", "\ue535"},
    {"AROMA_ICON_DIRECTIONS_WALK", "\ue536"},
    {"AROMA_ICON_DISC_FULL", "\ue610"},
    {"AROMA_ICON_DNS", "\ue875"},
    {"AROMA_ICON_DO_NOT_DISTURB", "\ue612"},
    {"AROMA_ICON_DO_NOT_DISTURB_ALT", "\ue611"},
    {"AROMA_ICON_DO_NOT_DISTURB_OFF", "\ue643"},
    {"AROMA_ICON_DO_NOT_DISTURB_ON", "\ue644"},
    {"AROMA_ICON_DOCK", "\ue30e"},
    {"AROMA_ICON_DOMAIN", "\ue7ee"},
    {"AROMA_ICON_DONE", "\ue876"},
    {"AROMA_ICON_DONE_ALL", "\ue877"},
    {"AROMA_ICON_DONUT_LARGE", "\ue917"},
    {"AROMA_ICON_DONUT_SMALL", "\ue918"},
    {"AROMA_ICON_DRAFTS", "\ue151"},
    {"AROMA_ICON_DRAG_HANDLE", "\ue25d"},
    {"AROMA_ICON_DRIVE_ETA", "\ue613"},
    {"AROMA_ICON_DVR", "\ue1b2"},
    {"AROMA_ICON_EDIT", "\ue3c9"},
    {"AROMA_ICON_EDIT_LOCATION", "\ue568"},
    {"AROMA_ICON_EJECT", "\ue8fb"},
    {"AROMA_ICON_EMAIL", "\ue0be"},
    {"AROMA_ICON_ENHANCED_ENCRYPTION", "\ue63f"},
    {"AROMA_ICON_EQUALIZER", "\ue01d"},
    {"AROMA_ICON_ERROR", "\ue000"},
    {"AROMA_ICON_ERROR_OUTLINE", "\ue001"},
    {"AROMA_ICON_EURO_SYMBOL", "\ue926"},
    {"AROMA_ICON_EV_STATION", "\ue56d"},
    {"AROMA_ICON_EVENT", "\ue878"},
    {"AROMA_ICON_EVENT_AVAILABLE", "\ue614"},
    {"AROMA_ICON_EVENT_BUSY", "\ue615"},
    {"AROMA_ICON_EVENT_NOTE", "\ue616"},
    {"AROMA_ICON_EVENT_SEAT", "\ue903"},
    {"AROMA_ICON_EXIT_TO_APP", "\ue879"},
    {"AROMA_ICON_EXPAND_LESS", "\ue5ce"},
    {"AROMA_ICON_EXPAND_MORE", "\ue5cf"},
    {"AROMA_ICON_EXPLICIT", "\ue01e"},
    {"AROMA_ICON_EXPLORE", "\ue87a"},
    {"AROMA_ICON_EXPOSURE", "\ue3ca"},
    {"AROMA_ICON_EXPOSURE_NEG_1", "\ue3cb"},
    {"AROMA_ICON_EXPOSURE_NEG_2", "\ue3cc"},
    {"AROMA_ICON_EXPOSURE_PLUS_1", "\ue3cd"},
    {"AROMA_ICON_EXPOSURE_PLUS_2", "\ue3ce"},
    {"AROMA_ICON_EXPOSURE_ZERO", "\ue3cf"},
    {"AROMA_ICON_EXTENSION", "\ue87b"},
    {"AROMA_ICON_FACE", "\ue87c"},
    {"AROMA_ICON_FAST_FORWARD", "\ue01f"},
    {"AROMA_ICON_FAST_REWIND", "\ue020"},
    {"AROMA_ICON_FAVORITE", "\ue87d"},
    {"AROMA_ICON_FAVORITE_BORDER", "\ue87e"},
    {"AROMA_ICON_FEATURED_PLAY_LIST", "\ue06d"},
    {"AROMA_ICON_FEATURED_VIDEO", "\ue06e"},
    {"AROMA_ICON_FEEDBACK", "\ue87f"},
    {"AROMA_ICON_FIBER_DVR", "\ue05d"},
    {"AROMA_ICON_FIBER_MANUAL_RECORD", "\ue061"},
    {"AROMA_ICON_FIBER_NEW", "\ue05e"},
    {"AROMA_ICON_FIBER_PIN", "\ue06a"},
    {"AROMA_ICON_FIBER_SMART_RECORD", "\ue062"},
    {"AROMA_ICON_FILE_DOWNLOAD", "\ue2c4"},
    {"AROMA_ICON_FILE_UPLOAD", "\ue2c6"},
    {"AROMA_ICON_FILTER", "\ue3d3"},
    {"AROMA_ICON_FILTER_1", "\ue3d0"},
    {"AROMA_ICON_FILTER_2", "\ue3d1"},
    {"AROMA_ICON_FILTER_3", "\ue3d2"},
    {"AROMA_ICON_FILTER_4", "\ue3d4"},
    {"AROMA_ICON_FILTER_5", "\ue3d5"},
    {"AROMA_ICON_FILTER_6", "\ue3d6"},
    {"AROMA_ICON_FILTER_7", "\ue3d7"},
    {"AROMA_ICON_FILTER_8", "\ue3d8"},
    {"AROMA_ICON_FILTER_9", "\ue3d9"},
    {"AROMA_ICON_FILTER_9_PLUS", "\ue3da"},
    {"AROMA_ICON_FILTER_B_AND_W", "\ue3db"},
    {"AROMA_ICON_FILTER_CENTER_FOCUS", "\ue3dc"},
    {"AROMA_ICON_FILTER_DRAMA", "\ue3dd"},
    {"AROMA_ICON_FILTER_FRAMES", "\ue3de"},
    {"AROMA_ICON_FILTER_HDR", "\ue3df"},
    {"AROMA_ICON_FILTER_LIST", "\ue152"},
    {"AROMA_ICON_FILTER_NONE", "\ue3e0"},
    {"AROMA_ICON_FILTER_TILT_SHIFT", "\ue3e2"},
    {"AROMA_ICON_FILTER_VINTAGE", "\ue3e3"},
    {"AROMA_ICON_FIND_IN_PAGE", "\ue880"},
    {"AROMA_ICON_FIND_REPLACE", "\ue881"},
    {"AROMA_ICON_FINGERPRINT", "\ue90d"},
    {"AROMA_ICON_FIRST_PAGE", "\ue5dc"},
    {"AROMA_ICON_FITNESS_CENTER", "\ueb43"},
    {"AROMA_ICON_FLAG", "\ue153"},
    {"AROMA_ICON_FLARE", "\ue3e4"},
    {"AROMA_ICON_FLASH_AUTO", "\ue3e5"},
    {"AROMA_ICON_FLASH_OFF", "\ue3e6"},
    {"AROMA_ICON_FLASH_ON", "\ue3e7"},
    {"AROMA_ICON_FLIGHT", "\ue539"},
    {"AROMA_ICON_FLIGHT_LAND", "\ue904"},
    {"AROMA_ICON_FLIGHT_TAKEOFF", "\ue905"},
    {"AROMA_ICON_FLIP", "\ue3e8"},
    {"AROMA_ICON_FLIP_TO_BACK", "\ue882"},
    {"AROMA_ICON_FLIP_TO_FRONT", "\ue883"},
    {"AROMA_ICON_FOLDER", "\ue2c7"},
    {"AROMA_ICON_FOLDER_OPEN", "\ue2c8"},
    {"AROMA_ICON_FOLDER_SHARED", "\ue2c9"},
    {"AROMA_ICON_FOLDER_SPECIAL", "\ue617"},
    {"AROMA_ICON_FONT_DOWNLOAD", "\ue167"},
    {"AROMA_ICON_FORMAT_ALIGN_CENTER", "\ue234"},
    {"AROMA_ICON_FORMAT_ALIGN_JUSTIFY", "\ue235"},
    {"AROMA_ICON_FORMAT_ALIGN_LEFT", "\ue236"},
    {"AROMA_ICON_FORMAT_ALIGN_RIGHT", "\ue237"},
    {"AROMA_ICON_FORMAT_BOLD", "\ue238"},
    {"AROMA_ICON_FORMAT_CLEAR", "\ue239"},
    {"AROMA_ICON_FORMAT_COLOR_FILL", "\ue23a"},
    {"AROMA_ICON_FORMAT_COLOR_RESET", "\ue23b"},
    {"AROMA_ICON_FORMAT_COLOR_TEXT", "\ue23c"},
    {"AROMA_ICON_FORMAT_INDENT_DECREASE", "\ue23d"},
    {"AROMA_ICON_FORMAT_INDENT_INCREASE", "\ue23e"},
    {"AROMA_ICON_FORMAT_ITALIC", "\ue23f"},
    {"AROMA_ICON_FORMAT_LINE_SPACING", "\ue240"},
    {"AROMA_ICON_FORMAT_LIST_BULLETED", "\ue241"},
    {"AROMA_ICON_FORMAT_LIST_NUMBERED", "\ue242"},
    {"AROMA_ICON_FORMAT_PAINT", "\ue243"},
    {"AROMA_ICON_FORMAT_QUOTE", "\ue244"},
    {"AROMA_ICON_FORMAT_SHAPES", "\ue25e"},
    {"AROMA_ICON_FORMAT_SIZE", "\ue245"},
    {"AROMA_ICON_FORMAT_STRIKETHROUGH", "\ue246"},
    {"AROMA_ICON_FORMAT_TEXTDIRECTION_L_TO_R", "\ue247"},
    {"AROMA_ICON_FORMAT_TEXTDIRECTION_R_TO_L", "\ue248"},
    {"AROMA_ICON_FORMAT_UNDERLINED", "\ue249"},
    {"AROMA_ICON_FORUM", "\ue0bf"},
    {"AROMA_ICON_FORWARD", "\ue154"},
    {"AROMA_ICON_FORWARD_10", "\ue056"},
    {"AROMA_ICON_FORWARD_30", "\ue057"},
    {"AROMA_ICON_FORWARD_5", "\ue058"},
    {"AROMA_ICON_FREE_BREAKFAST", "\ueb44"},
    {"AROMA_ICON_FULLSCREEN", "\ue5d0"},
    {"AROMA_ICON_FULLSCREEN_EXIT", "\ue5d1"},
    {"AROMA_ICON_FUNCTIONS", "\ue24a"},
    {"AROMA_ICON_G_TRANSLATE", "\ue927"},
    {"AROMA_ICON_GAMEPAD", "\ue30f"},
    {"AROMA_ICON_GAMES", "\ue021"},
    {"AROMA_ICON_GAVEL", "\ue90e"},
    {"AROMA_ICON_GESTURE", "\ue155"},
    {"AROMA_ICON_GET_APP", "\ue884"},
    {"AROMA_ICON_GIF", "\ue908"},
    {"AROMA_ICON_GOLF_COURSE", "\ueb45"},
    {"AROMA_ICON_GPS_FIXED", "\ue1b3"},
    {"AROMA_ICON_GPS_NOT_FIXED", "\ue1b4"},
    {"AROMA_ICON_GPS_OFF", "\ue1b5"},
    {"AROMA_ICON_GRADE", "\ue885"},
    {"AROMA_ICON_GRADIENT", "\ue3e9"},
    {"AROMA_ICON_GRAIN", "\ue3ea"},
    {"AROMA_ICON_GRAPHIC_EQ", "\ue1b8"},
    {"AROMA_ICON_GRID_OFF", "\ue3eb"},
    {"AROMA_ICON_GRID_ON", "\ue3ec"},
    {"AROMA_ICON_GROUP", "\ue7ef"},
    {"AROMA_ICON_GROUP_ADD", "\ue7f0"},
    {"AROMA_ICON_GROUP_WORK", "\ue886"},
    {"AROMA_ICON_HD", "\ue052"},
    {"AROMA_ICON_HDR_OFF", "\ue3ed"},
    {"AROMA_ICON_HDR_ON", "\ue3ee"},
    {"AROMA_ICON_HDR_STRONG", "\ue3f1"},
    {"AROMA_ICON_HDR_WEAK", "\ue3f2"},
    {"AROMA_ICON_HEADSET", "\ue310"},
    {"AROMA_ICON_HEADSET_MIC", "\ue311"},
    {"AROMA_ICON_HEALING", "\ue3f3"},
    {"AROMA_ICON_HEARING", "\ue023"},
    {"AROMA_ICON_HELP", "\ue887"},
    {"AROMA_ICON_HELP_OUTLINE", "\ue8fd"},
    {"AROMA_ICON_HIGH_QUALITY", "\ue024"},
    {"AROMA_ICON_HIGHLIGHT", "\ue25f"},
    {"AROMA_ICON_HIGHLIGHT_OFF", "\ue888"},
    {"AROMA_ICON_HISTORY", "\ue889"},
    {"AROMA_ICON_HOME", "\ue88a"},
    {"AROMA_ICON_HOTEL", "\ue53a"},
    {"AROMA_ICON_HOT_TUB", "\ueb46"},
    {"AROMA_ICON_HOURGLASS_EMPTY", "\ue88b"},
    {"AROMA_ICON_HOURGLASS_FULL", "\ue88c"},
    {"AROMA_ICON_HTTP", "\ue902"},
    {"AROMA_ICON_HTTPS", "\ue88d"},
    {"AROMA_ICON_IMAGE", "\ue3f4"},
    {"AROMA_ICON_IMAGE_ASPECT_RATIO", "\ue3f5"},
    {"AROMA_ICON_IMPORT_CONTACTS", "\ue0e0"},
    {"AROMA_ICON_IMPORT_EXPORT", "\ue0c3"},
    {"AROMA_ICON_IMPORTANT_DEVICES", "\ue912"},
    {"AROMA_ICON_INBOX", "\ue156"},
    {"AROMA_ICON_INDETERMINATE_CHECK_BOX", "\ue909"},
    {"AROMA_ICON_INFO", "\ue88e"},
    {"AROMA_ICON_INFO_OUTLINE", "\ue88f"},
    {"AROMA_ICON_INPUT", "\ue890"},
    {"AROMA_ICON_INSERT_CHART", "\ue24b"},
    {"AROMA_ICON_INSERT_COMMENT", "\ue24c"},
    {"AROMA_ICON_INSERT_DRIVE_FILE", "\ue24d"},
    {"AROMA_ICON_INSERT_EMOTICON", "\ue24e"},
    {"AROMA_ICON_INSERT_INVITATION", "\ue24f"},
    {"AROMA_ICON_INSERT_LINK", "\ue250"},
    {"AROMA_ICON_INSERT_PHOTO", "\ue251"},
    {"AROMA_ICON_INVERT_COLORS", "\ue891"},
    {"AROMA_ICON_INVERT_COLORS_OFF", "\ue0c4"},
    {"AROMA_ICON_ISO", "\ue3f6"},
    {"AROMA_ICON_KEYBOARD", "\ue312"},
    {"AROMA_ICON_KEYBOARD_ARROW_DOWN", "\ue313"},
    {"AROMA_ICON_KEYBOARD_ARROW_LEFT", "\ue314"},
    {"AROMA_ICON_KEYBOARD_ARROW_RIGHT", "\ue315"},
    {"AROMA_ICON_KEYBOARD_ARROW_UP", "\ue316"},
    {"AROMA_ICON_KEYBOARD_BACKSPACE", "\ue317"},
    {"AROMA_ICON_KEYBOARD_CAPSLOCK", "\ue318"},
    {"AROMA_ICON_KEYBOARD_HIDE", "\ue31a"},
    {"AROMA_ICON_KEYBOARD_RETURN", "\ue31b"},
    {"AROMA_ICON_KEYBOARD_TAB", "\ue31c"},
    {"AROMA_ICON_KEYBOARD_VOICE", "\ue31d"},
    {"AROMA_ICON_KITCHEN", "\ueb47"},
    {"AROMA_ICON_LABEL", "\ue892"},
    {"AROMA_ICON_LABEL_OUTLINE", "\ue893"},
    {"AROMA_ICON_LANDSCAPE", "\ue3f7"},
    {"AROMA_ICON_LANGUAGE", "\ue894"},
    {"AROMA_ICON_LAPTOP", "\ue31e"},
    {"AROMA_ICON_LAPTOP_CHROMEBOOK", "\ue31f"},
    {"AROMA_ICON_LAPTOP_MAC", "\ue320"},
    {"AROMA_ICON_LAPTOP_WINDOWS", "\ue321"},
    {"AROMA_ICON_LAST_PAGE", "\ue5dd"},
    {"AROMA_ICON_LAUNCH", "\ue895"},
    {"AROMA_ICON_LAYERS", "\ue53b"},
    {"AROMA_ICON_LAYERS_CLEAR", "\ue53c"},
    {"AROMA_ICON_LEAK_ADD", "\ue3f8"},
    {"AROMA_ICON_LEAK_REMOVE", "\ue3f9"},
    {"AROMA_ICON_LENS", "\ue3fa"},
    {"AROMA_ICON_LIBRARY_ADD", "\ue02e"},
    {"AROMA_ICON_LIBRARY_BOOKS", "\ue02f"},
    {"AROMA_ICON_LIBRARY_MUSIC", "\ue030"},
    {"AROMA_ICON_LIGHTBULB_OUTLINE", "\ue90f"},
    {"AROMA_ICON_LINE_STYLE", "\ue919"},
    {"AROMA_ICON_LINE_WEIGHT", "\ue91a"},
    {"AROMA_ICON_LINEAR_SCALE", "\ue260"},
    {"AROMA_ICON_LINK", "\ue157"},
    {"AROMA_ICON_LINKED_CAMERA", "\ue438"},
    {"AROMA_ICON_LIST", "\ue896"},
    {"AROMA_ICON_LIVE_HELP", "\ue0c6"},
    {"AROMA_ICON_LIVE_TV", "\ue639"},
    {"AROMA_ICON_LOCAL_ACTIVITY", "\ue53f"},
    {"AROMA_ICON_LOCAL_AIRPORT", "\ue53d"},
    {"AROMA_ICON_LOCAL_ATM", "\ue53e"},
    {"AROMA_ICON_LOCAL_BAR", "\ue540"},
    {"AROMA_ICON_LOCAL_CAFE", "\ue541"},
    {"AROMA_ICON_LOCAL_CAR_WASH", "\ue542"},
    {"AROMA_ICON_LOCAL_CONVENIENCE_STORE", "\ue543"},
    {"AROMA_ICON_LOCAL_DINING", "\ue556"},
    {"AROMA_ICON_LOCAL_DRINK", "\ue544"},
    {"AROMA_ICON_LOCAL_FLORIST", "\ue545"},
    {"AROMA_ICON_LOCAL_GAS_STATION", "\ue546"},
    {"AROMA_ICON_LOCAL_GROCERY_STORE", "\ue547"},
    {"AROMA_ICON_LOCAL_HOSPITAL", "\ue548"},
    {"AROMA_ICON_LOCAL_HOTEL", "\ue549"},
    {"AROMA_ICON_LOCAL_LAUNDRY_SERVICE", "\ue54a"},
    {"AROMA_ICON_LOCAL_LIBRARY", "\ue54b"},
    {"AROMA_ICON_LOCAL_MALL", "\ue54c"},
    {"AROMA_ICON_LOCAL_MOVIES", "\ue54d"},
    {"AROMA_ICON_LOCAL_OFFER", "\ue54e"},
    {"AROMA_ICON_LOCAL_PARKING", "\ue54f"},
    {"AROMA_ICON_LOCAL_PHARMACY", "\ue550"},
    {"AROMA_ICON_LOCAL_PHONE", "\ue551"},
    {"AROMA_ICON_LOCAL_PIZZA", "\ue552"},
    {"AROMA_ICON_LOCAL_PLAY", "\ue553"},
    {"AROMA_ICON_LOCAL_POST_OFFICE", "\ue554"},
    {"AROMA_ICON_LOCAL_PRINTSHOP", "\ue555"},
    {"AROMA_ICON_LOCAL_SEE", "\ue557"},
    {"AROMA_ICON_LOCAL_SHIPPING", "\ue558"},
    {"AROMA_ICON_LOCAL_TAXI", "\ue559"},
    {"AROMA_ICON_LOCATION_CITY", "\ue7f1"},
    {"AROMA_ICON_LOCATION_DISABLED", "\ue1b6"},
    {"AROMA_ICON_LOCATION_OFF", "\ue0c7"},
    {"AROMA_ICON_LOCATION_ON", "\ue0c8"},
    {"AROMA_ICON_LOCATION_SEARCHING", "\ue1b7"},
    {"AROMA_ICON_LOCK", "\ue897"},
    {"AROMA_ICON_LOCK_OPEN", "\ue898"},
    {"AROMA_ICON_LOCK_OUTLINE", "\ue899"},
    {"AROMA_ICON_LOOKS", "\ue3fc"},
    {"AROMA_ICON_LOOKS_3", "\ue3fb"},
    {"AROMA_ICON_LOOKS_4", "\ue3fd"},
    {"AROMA_ICON_LOOKS_5", "\ue3fe"},
    {"AROMA_ICON_LOOKS_6", "\ue3ff"},
    {"AROMA_ICON_LOOKS_ONE", "\ue400"},
    {"AROMA_ICON_LOOKS_TWO", "\ue401"},
    {"AROMA_ICON_LOOP", "\ue028"},
    {"AROMA_ICON_LOUPE", "\ue402"},
    {"AROMA_ICON_LOW_PRIORITY", "\ue16d"},
    {"AROMA_ICON_LOYALTY", "\ue89a"},
    {"AROMA_ICON_MAIL", "\ue158"},
    {"AROMA_ICON_MAIL_OUTLINE", "\ue0e1"},
    {"AROMA_ICON_MAP", "\ue55b"},
    {"AROMA_ICON_MARKUNREAD", "\ue159"},
    {"AROMA_ICON_MARKUNREAD_MAILBOX", "\ue89b"},
    {"AROMA_ICON_MEMORY", "\ue322"},
    {"AROMA_ICON_MENU", "\ue5d2"},
    {"AROMA_ICON_MERGE_TYPE", "\ue252"},
    {"AROMA_ICON_MESSAGE", "\ue0c9"},
    {"AROMA_ICON_MIC", "\ue029"},
    {"AROMA_ICON_MIC_NONE", "\ue02a"},
    {"AROMA_ICON_MIC_OFF", "\ue02b"},
    {"AROMA_ICON_MMS", "\ue618"},
    {"AROMA_ICON_MODE_COMMENT", "\ue253"},
    {"AROMA_ICON_MODE_EDIT", "\ue254"},
    {"AROMA_ICON_MONETIZATION_ON", "\ue263"},
    {"AROMA_ICON_MONEY_OFF", "\ue25c"},
    {"AROMA_ICON_MONOCHROME_PHOTOS", "\ue403"},
    {"AROMA_ICON_MOOD", "\ue7f2"},
    {"AROMA_ICON_MOOD_BAD", "\ue7f3"},
    {"AROMA_ICON_MORE", "\ue619"},
    {"AROMA_ICON_MORE_HORIZ", "\ue5d3"},
    {"AROMA_ICON_MORE_VERT", "\ue5d4"},
    {"AROMA_ICON_MOTORCYCLE", "\ue91b"},
    {"AROMA_ICON_MOUSE", "\ue323"},
    {"AROMA_ICON_MOVE_TO_INBOX", "\ue168"},
    {"AROMA_ICON_MOVIE", "\ue02c"},
    {"AROMA_ICON_MOVIE_CREATION", "\ue404"},
    {"AROMA_ICON_MOVIE_FILTER", "\ue43a"},
    {"AROMA_ICON_MULTILINE_CHART", "\ue6df"},
    {"AROMA_ICON_MUSIC_NOTE", "\ue405"},
    {"AROMA_ICON_MUSIC_VIDEO", "\ue063"},
    {"AROMA_ICON_MY_LOCATION", "\ue55c"},
    {"AROMA_ICON_NATURE", "\ue406"},
    {"AROMA_ICON_NATURE_PEOPLE", "\ue407"},
    {"AROMA_ICON_NAVIGATE_BEFORE", "\ue408"},
    {"AROMA_ICON_NAVIGATE_NEXT", "\ue409"},
    {"AROMA_ICON_NAVIGATION", "\ue55d"},
    {"AROMA_ICON_NEAR_ME", "\ue569"},
    {"AROMA_ICON_NETWORK_CELL", "\ue1b9"},
    {"AROMA_ICON_NETWORK_CHECK", "\ue640"},
    {"AROMA_ICON_NETWORK_LOCKED", "\ue61a"},
    {"AROMA_ICON_NETWORK_WIFI", "\ue1ba"},
    {"AROMA_ICON_NEW_RELEASES", "\ue031"},
    {"AROMA_ICON_NEXT_WEEK", "\ue16a"},
    {"AROMA_ICON_NFC", "\ue1bb"},
    {"AROMA_ICON_NO_ENCRYPTION", "\ue641"},
    {"AROMA_ICON_NO_SIM", "\ue0cc"},
    {"AROMA_ICON_NOT_INTERESTED", "\ue033"},
    {"AROMA_ICON_NOTE", "\ue06f"},
    {"AROMA_ICON_NOTE_ADD", "\ue89c"},
    {"AROMA_ICON_NOTIFICATIONS", "\ue7f4"},
    {"AROMA_ICON_NOTIFICATIONS_ACTIVE", "\ue7f7"},
    {"AROMA_ICON_NOTIFICATIONS_NONE", "\ue7f5"},
    {"AROMA_ICON_NOTIFICATIONS_OFF", "\ue7f6"},
    {"AROMA_ICON_NOTIFICATIONS_PAUSED", "\ue7f8"},
    {"AROMA_ICON_OFFLINE_PIN", "\ue90a"},
    {"AROMA_ICON_ONDEMAND_VIDEO", "\ue63a"},
    {"AROMA_ICON_OPACITY", "\ue91c"},
    {"AROMA_ICON_OPEN_IN_BROWSER", "\ue89d"},
    {"AROMA_ICON_OPEN_IN_NEW", "\ue89e"},
    {"AROMA_ICON_OPEN_WITH", "\ue89f"},
    {"AROMA_ICON_PAGES", "\ue7f9"},
    {"AROMA_ICON_PAGEVIEW", "\ue8a0"},
    {"AROMA_ICON_PALETTE", "\ue40a"},
    {"AROMA_ICON_PAN_TOOL", "\ue925"},
    {"AROMA_ICON_PANORAMA", "\ue40b"},
    {"AROMA_ICON_PANORAMA_FISH_EYE", "\ue40c"},
    {"AROMA_ICON_PANORAMA_HORIZONTAL", "\ue40d"},
    {"AROMA_ICON_PANORAMA_VERTICAL", "\ue40e"},
    {"AROMA_ICON_PANORAMA_WIDE_ANGLE", "\ue40f"},
    {"AROMA_ICON_PARTY_MODE", "\ue7fa"},
    {"AROMA_ICON_PAUSE", "\ue034"},
    {"AROMA_ICON_PAUSE_CIRCLE_FILLED", "\ue035"},
    {"AROMA_ICON_PAUSE_CIRCLE_OUTLINE", "\ue036"},
    {"AROMA_ICON_PAYMENT", "\ue8a1"},
    {"AROMA_ICON_PEOPLE", "\ue7fb"},
    {"AROMA_ICON_PEOPLE_OUTLINE", "\ue7fc"},
    {"AROMA_ICON_PERM_CAMERA_MIC", "\ue8a2"},
    {"AROMA_ICON_PERM_CONTACT_CALENDAR", "\ue8a3"},
    {"AROMA_ICON_PERM_DATA_SETTING", "\ue8a4"},
    {"AROMA_ICON_PERM_DEVICE_INFORMATION", "\ue8a5"},
    {"AROMA_ICON_PERM_IDENTITY", "\ue8a6"},
    {"AROMA_ICON_PERM_MEDIA", "\ue8a7"},
    {"AROMA_ICON_PERM_PHONE_MSG", "\ue8a8"},
    {"AROMA_ICON_PERM_SCAN_WIFI", "\ue8a9"},
    {"AROMA_ICON_PERSON", "\ue7fd"},
    {"AROMA_ICON_PERSON_ADD", "\ue7fe"},
    {"AROMA_ICON_PERSON_OUTLINE", "\ue7ff"},
    {"AROMA_ICON_PERSON_PIN", "\ue55a"},
    {"AROMA_ICON_PERSON_PIN_CIRCLE", "\ue56a"},
    {"AROMA_ICON_PERSONAL_VIDEO", "\ue63b"},
    {"AROMA_ICON_PETS", "\ue91d"},
    {"AROMA_ICON_PHONE", "\ue0cd"},
    {"AROMA_ICON_PHONE_ANDROID", "\ue324"},
    {"AROMA_ICON_PHONE_BLUETOOTH_SPEAKER", "\ue61b"},
    {"AROMA_ICON_PHONE_FORWARDED", "\ue61c"},
    {"AROMA_ICON_PHONE_IN_TALK", "\ue61d"},
    {"AROMA_ICON_PHONE_IPHONE", "\ue325"},
    {"AROMA_ICON_PHONE_LOCKED", "\ue61e"},
    {"AROMA_ICON_PHONE_MISSED", "\ue61f"},
    {"AROMA_ICON_PHONE_PAUSED", "\ue620"},
    {"AROMA_ICON_PHONELINK", "\ue326"},
    {"AROMA_ICON_PHONELINK_ERASE", "\ue0db"},
    {"AROMA_ICON_PHONELINK_LOCK", "\ue0dc"},
    {"AROMA_ICON_PHONELINK_OFF", "\ue327"},
    {"AROMA_ICON_PHONELINK_RING", "\ue0dd"},
    {"AROMA_ICON_PHONELINK_SETUP", "\ue0de"},
    {"AROMA_ICON_PHOTO", "\ue410"},
    {"AROMA_ICON_PHOTO_ALBUM", "\ue411"},
    {"AROMA_ICON_PHOTO_CAMERA", "\ue412"},
    {"AROMA_ICON_PHOTO_FILTER", "\ue43b"},
    {"AROMA_ICON_PHOTO_LIBRARY", "\ue413"},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_ACTUAL", "\ue432"},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_LARGE", "\ue433"},
    {"AROMA_ICON_PHOTO_SIZE_SELECT_SMALL", "\ue434"},
    {"AROMA_ICON_PICTURE_AS_PDF", "\ue415"},
    {"AROMA_ICON_PICTURE_IN_PICTURE", "\ue8aa"},
    {"AROMA_ICON_PICTURE_IN_PICTURE_ALT", "\ue911"},
    {"AROMA_ICON_PIE_CHART", "\ue6c4"},
    {"AROMA_ICON_PIE_CHART_OUTLINED", "\ue6c5"},
    {"AROMA_ICON_PIN_DROP", "\ue55e"},
    {"AROMA_ICON_PLACE", "\ue55f"},
    {"AROMA_ICON_PLAY_ARROW", "\ue037"},
    {"AROMA_ICON_PLAY_CIRCLE_FILLED", "\ue038"},
    {"AROMA_ICON_PLAY_CIRCLE_OUTLINE", "\ue039"},
    {"AROMA_ICON_PLAY_FOR_WORK", "\ue906"},
    {"AROMA_ICON_PLAYLIST_ADD", "\ue03b"},
    {"AROMA_ICON_PLAYLIST_ADD_CHECK", "\ue065"},
    {"AROMA_ICON_PLAYLIST_PLAY", "\ue05f"},
    {"AROMA_ICON_PLUS_ONE", "\ue800"},
    {"AROMA_ICON_POLL", "\ue801"},
    {"AROMA_ICON_POLYMER", "\ue8ab"},
    {"AROMA_ICON_POOL", "\ueb48"},
    {"AROMA_ICON_PORTABLE_WIFI_OFF", "\ue0ce"},
    {"AROMA_ICON_PORTRAIT", "\ue416"},
    {"AROMA_ICON_POWER", "\ue63c"},
    {"AROMA_ICON_POWER_INPUT", "\ue336"},
    {"AROMA_ICON_POWER_SETTINGS_NEW", "\ue8ac"},
    {"AROMA_ICON_PREGNANT_WOMAN", "\ue91e"},
    {"AROMA_ICON_PRESENT_TO_ALL", "\ue0df"},
    {"AROMA_ICON_PRINT", "\ue8ad"},
    {"AROMA_ICON_PRIORITY_HIGH", "\ue645"},
    {"AROMA_ICON_PUBLIC", "\ue80b"},
    {"AROMA_ICON_PUBLISH", "\ue255"},
    {"AROMA_ICON_QUERY_BUILDER", "\ue8ae"},
    {"AROMA_ICON_QUESTION_ANSWER", "\ue8af"},
    {"AROMA_ICON_QUEUE", "\ue03c"},
    {"AROMA_ICON_QUEUE_MUSIC", "\ue03d"},
    {"AROMA_ICON_QUEUE_PLAY_NEXT", "\ue066"},
    {"AROMA_ICON_RADIO", "\ue03e"},
    {"AROMA_ICON_RADIO_BUTTON_CHECKED", "\ue837"},
    {"AROMA_ICON_RADIO_BUTTON_UNCHECKED", "\ue836"},
    {"AROMA_ICON_RATE_REVIEW", "\ue560"},
    {"AROMA_ICON_RECEIPT", "\ue8b0"},
    {"AROMA_ICON_RECENT_ACTORS", "\ue03f"},
    {"AROMA_ICON_RECORD_VOICE_OVER", "\ue91f"},
    {"AROMA_ICON_REDEEM", "\ue8b1"},
    {"AROMA_ICON_REDO", "\ue15a"},
    {"AROMA_ICON_REFRESH", "\ue5d5"},
    {"AROMA_ICON_REMOVE", "\ue15b"},
    {"AROMA_ICON_REMOVE_CIRCLE", "\ue15c"},
    {"AROMA_ICON_REMOVE_CIRCLE_OUTLINE", "\ue15d"},
    {"AROMA_ICON_REMOVE_FROM_QUEUE", "\ue067"},
    {"AROMA_ICON_REMOVE_RED_EYE", "\ue417"},
    {"AROMA_ICON_REMOVE_SHOPPING_CART", "\ue928"},
    {"AROMA_ICON_REORDER", "\ue8fe"},
    {"AROMA_ICON_REPEAT", "\ue040"},
    {"AROMA_ICON_REPEAT_ONE", "\ue041"},
    {"AROMA_ICON_REPLAY", "\ue042"},
    {"AROMA_ICON_REPLAY_10", "\ue059"},
    {"AROMA_ICON_REPLAY_30", "\ue05a"},
    {"AROMA_ICON_REPLAY_5", "\ue05b"},
    {"AROMA_ICON_REPLY", "\ue15e"},
    {"AROMA_ICON_REPLY_ALL", "\ue15f"},
    {"AROMA_ICON_REPORT", "\ue160"},
    {"AROMA_ICON_REPORT_PROBLEM", "\ue8b2"},
    {"AROMA_ICON_RESTAURANT", "\ue56c"},
    {"AROMA_ICON_RESTAURANT_MENU", "\ue561"},
    {"AROMA_ICON_RESTORE", "\ue8b3"},
    {"AROMA_ICON_RESTORE_PAGE", "\ue929"},
    {"AROMA_ICON_RING_VOLUME", "\ue0d1"},
    {"AROMA_ICON_ROOM", "\ue8b4"},
    {"AROMA_ICON_ROOM_SERVICE", "\ueb49"},
    {"AROMA_ICON_ROTATE_90_DEGREES_CCW", "\ue418"},
    {"AROMA_ICON_ROTATE_LEFT", "\ue419"},
    {"AROMA_ICON_ROTATE_RIGHT", "\ue41a"},
    {"AROMA_ICON_ROUNDED_CORNER", "\ue920"},
    {"AROMA_ICON_ROUTER", "\ue328"},
    {"AROMA_ICON_ROWING", "\ue921"},
    {"AROMA_ICON_RSS_FEED", "\ue0e5"},
    {"AROMA_ICON_RV_HOOKUP", "\ue642"},
    {"AROMA_ICON_SATELLITE", "\ue562"},
    {"AROMA_ICON_SAVE", "\ue161"},
    {"AROMA_ICON_SCANNER", "\ue329"},
    {"AROMA_ICON_SCHEDULE", "\ue8b5"},
    {"AROMA_ICON_SCHOOL", "\ue80c"},
    {"AROMA_ICON_SCREEN_LOCK_LANDSCAPE", "\ue1be"},
    {"AROMA_ICON_SCREEN_LOCK_PORTRAIT", "\ue1bf"},
    {"AROMA_ICON_SCREEN_LOCK_ROTATION", "\ue1c0"},
    {"AROMA_ICON_SCREEN_ROTATION", "\ue1c1"},
    {"AROMA_ICON_SCREEN_SHARE", "\ue0e2"},
    {"AROMA_ICON_SD_CARD", "\ue623"},
    {"AROMA_ICON_SD_STORAGE", "\ue1c2"},
    {"AROMA_ICON_SEARCH", "\ue8b6"},
    {"AROMA_ICON_SECURITY", "\ue32a"},
    {"AROMA_ICON_SELECT_ALL", "\ue162"},
    {"AROMA_ICON_SEND", "\ue163"},
    {"AROMA_ICON_SENTIMENT_DISSATISFIED", "\ue811"},
    {"AROMA_ICON_SENTIMENT_NEUTRAL", "\ue812"},
    {"AROMA_ICON_SENTIMENT_SATISFIED", "\ue813"},
    {"AROMA_ICON_SENTIMENT_VERY_DISSATISFIED", "\ue814"},
    {"AROMA_ICON_SENTIMENT_VERY_SATISFIED", "\ue815"},
    {"AROMA_ICON_SETTINGS", "\ue8b8"},
    {"AROMA_ICON_SETTINGS_APPLICATIONS", "\ue8b9"},
    {"AROMA_ICON_SETTINGS_BACKUP_RESTORE", "\ue8ba"},
    {"AROMA_ICON_SETTINGS_BLUETOOTH", "\ue8bb"},
    {"AROMA_ICON_SETTINGS_BRIGHTNESS", "\ue8bd"},
    {"AROMA_ICON_SETTINGS_CELL", "\ue8bc"},
    {"AROMA_ICON_SETTINGS_ETHERNET", "\ue8be"},
    {"AROMA_ICON_SETTINGS_INPUT_ANTENNA", "\ue8bf"},
    {"AROMA_ICON_SETTINGS_INPUT_COMPONENT", "\ue8c0"},
    {"AROMA_ICON_SETTINGS_INPUT_COMPOSITE", "\ue8c1"},
    {"AROMA_ICON_SETTINGS_INPUT_HDMI", "\ue8c2"},
    {"AROMA_ICON_SETTINGS_INPUT_SVIDEO", "\ue8c3"},
    {"AROMA_ICON_SETTINGS_OVERSCAN", "\ue8c4"},
    {"AROMA_ICON_SETTINGS_PHONE", "\ue8c5"},
    {"AROMA_ICON_SETTINGS_POWER", "\ue8c6"},
    {"AROMA_ICON_SETTINGS_REMOTE", "\ue8c7"},
    {"AROMA_ICON_SETTINGS_SYSTEM_DAYDREAM", "\ue1c3"},
    {"AROMA_ICON_SETTINGS_VOICE", "\ue8c8"},
    {"AROMA_ICON_SHARE", "\ue80d"},
    {"AROMA_ICON_SHOP", "\ue8c9"},
    {"AROMA_ICON_SHOP_TWO", "\ue8ca"},
    {"AROMA_ICON_SHOPPING_BASKET", "\ue8cb"},
    {"AROMA_ICON_SHOPPING_CART", "\ue8cc"},
    {"AROMA_ICON_SHORT_TEXT", "\ue261"},
    {"AROMA_ICON_SHOW_CHART", "\ue6e1"},
    {"AROMA_ICON_SHUFFLE", "\ue043"},
    {"AROMA_ICON_SIGNAL_CELLULAR_4_BAR", "\ue1c8"},
    {"AROMA_ICON_SIGNAL_CELLULAR_CONNECTED_NO_INTERNET_4_BAR", "\ue1cd"},
    {"AROMA_ICON_SIGNAL_CELLULAR_NO_SIM", "\ue1ce"},
    {"AROMA_ICON_SIGNAL_CELLULAR_NULL", "\ue1cf"},
    {"AROMA_ICON_SIGNAL_CELLULAR_OFF", "\ue1d0"},
    {"AROMA_ICON_SIGNAL_WIFI_4_BAR", "\ue1d8"},
    {"AROMA_ICON_SIGNAL_WIFI_4_BAR_LOCK", "\ue1d9"},
    {"AROMA_ICON_SIGNAL_WIFI_OFF", "\ue1da"},
    {"AROMA_ICON_SIM_CARD", "\ue32b"},
    {"AROMA_ICON_SIM_CARD_ALERT", "\ue624"},
    {"AROMA_ICON_SKIP_NEXT", "\ue044"},
    {"AROMA_ICON_SKIP_PREVIOUS", "\ue045"},
    {"AROMA_ICON_SLIDESHOW", "\ue41b"},
    {"AROMA_ICON_SLOW_MOTION_VIDEO", "\ue068"},
    {"AROMA_ICON_SMARTPHONE", "\ue32c"},
    {"AROMA_ICON_SMOKE_FREE", "\ueb4a"},
    {"AROMA_ICON_SMOKING_ROOMS", "\ueb4b"},
    {"AROMA_ICON_SMS", "\ue625"},
    {"AROMA_ICON_SMS_FAILED", "\ue626"},
    {"AROMA_ICON_SNOOZE", "\ue046"},
    {"AROMA_ICON_SORT", "\ue164"},
    {"AROMA_ICON_SORT_BY_ALPHA", "\ue053"},
    {"AROMA_ICON_SPA", "\ueb4c"},
    {"AROMA_ICON_SPACE_BAR", "\ue256"},
    {"AROMA_ICON_SPEAKER", "\ue32d"},
    {"AROMA_ICON_SPEAKER_GROUP", "\ue32e"},
    {"AROMA_ICON_SPEAKER_NOTES", "\ue8cd"},
    {"AROMA_ICON_SPEAKER_NOTES_OFF", "\ue92a"},
    {"AROMA_ICON_SPEAKER_PHONE", "\ue0d2"},
    {"AROMA_ICON_SPELLCHECK", "\ue8ce"},
    {"AROMA_ICON_STAR", "\ue838"},
    {"AROMA_ICON_STAR_BORDER", "\ue83a"},
    {"AROMA_ICON_STAR_HALF", "\ue839"},
    {"AROMA_ICON_STARS", "\ue8d0"},
    {"AROMA_ICON_STAY_CURRENT_LANDSCAPE", "\ue0d3"},
    {"AROMA_ICON_STAY_CURRENT_PORTRAIT", "\ue0d4"},
    {"AROMA_ICON_STAY_PRIMARY_LANDSCAPE", "\ue0d5"},
    {"AROMA_ICON_STAY_PRIMARY_PORTRAIT", "\ue0d6"},
    {"AROMA_ICON_STOP", "\ue047"},
    {"AROMA_ICON_STOP_SCREEN_SHARE", "\ue0e3"},
    {"AROMA_ICON_STORAGE", "\ue1db"},
    {"AROMA_ICON_STORE", "\ue8d1"},
    {"AROMA_ICON_STORE_MALL_DIRECTORY", "\ue563"},
    {"AROMA_ICON_STRAIGHTEN", "\ue41c"},
    {"AROMA_ICON_STREETVIEW", "\ue56e"},
    {"AROMA_ICON_STRIKETHROUGH_S", "\ue257"},
    {"AROMA_ICON_STYLE", "\ue41d"},
    {"AROMA_ICON_SUBDIRECTORY_ARROW_LEFT", "\ue5d9"},
    {"AROMA_ICON_SUBDIRECTORY_ARROW_RIGHT", "\ue5da"},
    {"AROMA_ICON_SUBJECT", "\ue8d2"},
    {"AROMA_ICON_SUBSCRIPTIONS", "\ue064"},
    {"AROMA_ICON_SUBTITLES", "\ue048"},
    {"AROMA_ICON_SUBWAY", "\ue56f"},
    {"AROMA_ICON_SUPERVISOR_ACCOUNT", "\ue8d3"},
    {"AROMA_ICON_SURROUND_SOUND", "\ue049"},
    {"AROMA_ICON_SWAP_CALLS", "\ue0d7"},
    {"AROMA_ICON_SWAP_HORIZ", "\ue8d4"},
    {"AROMA_ICON_SWAP_VERT", "\ue8d5"},
    {"AROMA_ICON_SWAP_VERTICAL_CIRCLE", "\ue8d6"},
    {"AROMA_ICON_SWITCH_CAMERA", "\ue41e"},
    {"AROMA_ICON_SWITCH_VIDEO", "\ue41f"},
    {"AROMA_ICON_SYNC", "\ue627"},
    {"AROMA_ICON_SYNC_DISABLED", "\ue628"},
    {"AROMA_ICON_SYNC_PROBLEM", "\ue629"},
    {"AROMA_ICON_SYSTEM_UPDATE", "\ue62a"},
    {"AROMA_ICON_SYSTEM_UPDATE_ALT", "\ue8d7"},
    {"AROMA_ICON_TAB", "\ue8d8"},
    {"AROMA_ICON_TAB_UNSELECTED", "\ue8d9"},
    {"AROMA_ICON_TABLET", "\ue32f"},
    {"AROMA_ICON_TABLET_ANDROID", "\ue330"},
    {"AROMA_ICON_TABLET_MAC", "\ue331"},
    {"AROMA_ICON_TAG_FACES", "\ue420"},
    {"AROMA_ICON_TAP_AND_PLAY", "\ue62b"},
    {"AROMA_ICON_TERRAIN", "\ue564"},
    {"AROMA_ICON_TEXT_FIELDS", "\ue262"},
    {"AROMA_ICON_TEXT_FORMAT", "\ue165"},
    {"AROMA_ICON_TEXTSMS", "\ue0d8"},
    {"AROMA_ICON_TEXTURE", "\ue421"},
    {"AROMA_ICON_THEATERS", "\ue8da"},
    {"AROMA_ICON_THUMB_DOWN", "\ue8db"},
    {"AROMA_ICON_THUMB_UP", "\ue8dc"},
    {"AROMA_ICON_THUMBS_UP_DOWN", "\ue8dd"},
    {"AROMA_ICON_TIME_TO_LEAVE", "\ue62c"},
    {"AROMA_ICON_TIMELAPSE", "\ue422"},
    {"AROMA_ICON_TIMELINE", "\ue922"},
    {"AROMA_ICON_TIMER", "\ue425"},
    {"AROMA_ICON_TIMER_10", "\ue423"},
    {"AROMA_ICON_TIMER_3", "\ue424"},
    {"AROMA_ICON_TIMER_OFF", "\ue426"},
    {"AROMA_ICON_TITLE", "\ue264"},
    {"AROMA_ICON_TOC", "\ue8de"},
    {"AROMA_ICON_TODAY", "\ue8df"},
    {"AROMA_ICON_TOLL", "\ue8e0"},
    {"AROMA_ICON_TONALITY", "\ue427"},
    {"AROMA_ICON_TOUCH_APP", "\ue913"},
    {"AROMA_ICON_TOYS", "\ue332"},
    {"AROMA_ICON_TRACK_CHANGES", "\ue8e1"},
    {"AROMA_ICON_TRAFFIC", "\ue565"},
    {"AROMA_ICON_TRAIN", "\ue570"},
    {"AROMA_ICON_TRAM", "\ue571"},
    {"AROMA_ICON_TRANSFER_WITHIN_A_STATION", "\ue572"},
    {"AROMA_ICON_TRANSFORM", "\ue428"},
    {"AROMA_ICON_TRANSLATE", "\ue8e2"},
    {"AROMA_ICON_TRENDING_DOWN", "\ue8e3"},
    {"AROMA_ICON_TRENDING_FLAT", "\ue8e4"},
    {"AROMA_ICON_TRENDING_UP", "\ue8e5"},
    {"AROMA_ICON_TUNE", "\ue429"},
    {"AROMA_ICON_TURNED_IN", "\ue8e6"},
    {"AROMA_ICON_TURNED_IN_NOT", "\ue8e7"},
    {"AROMA_ICON_TV", "\ue333"},
    {"AROMA_ICON_UNARCHIVE", "\ue169"},
    {"AROMA_ICON_UNDO", "\ue166"},
    {"AROMA_ICON_UNFOLD_LESS", "\ue5d6"},
    {"AROMA_ICON_UNFOLD_MORE", "\ue5d7"},
    {"AROMA_ICON_UPDATE", "\ue923"},
    {"AROMA_ICON_USB", "\ue1e0"},
    {"AROMA_ICON_VERIFIED_USER", "\ue8e8"},
    {"AROMA_ICON_VERTICAL_ALIGN_BOTTOM", "\ue258"},
    {"AROMA_ICON_VERTICAL_ALIGN_CENTER", "\ue259"},
    {"AROMA_ICON_VERTICAL_ALIGN_TOP", "\ue25a"},
    {"AROMA_ICON_VIBRATION", "\ue62d"},
    {"AROMA_ICON_VIDEO_CALL", "\ue070"},
    {"AROMA_ICON_VIDEO_LABEL", "\ue071"},
    {"AROMA_ICON_VIDEO_LIBRARY", "\ue04a"},
    {"AROMA_ICON_VIDEOCAM", "\ue04b"},
    {"AROMA_ICON_VIDEOCAM_OFF", "\ue04c"},
    {"AROMA_ICON_VIDEOGAME_ASSET", "\ue338"},
    {"AROMA_ICON_VIEW_AGENDA", "\ue8e9"},
    {"AROMA_ICON_VIEW_ARRAY", "\ue8ea"},
    {"AROMA_ICON_VIEW_CAROUSEL", "\ue8eb"},
    {"AROMA_ICON_VIEW_COLUMN", "\ue8ec"},
    {"AROMA_ICON_VIEW_COMFY", "\ue42a"},
    {"AROMA_ICON_VIEW_COMPACT", "\ue42b"},
    {"AROMA_ICON_VIEW_DAY", "\ue8ed"},
    {"AROMA_ICON_VIEW_HEADLINE", "\ue8ee"},
    {"AROMA_ICON_VIEW_LIST", "\ue8ef"},
    {"AROMA_ICON_VIEW_MODULE", "\ue8f0"},
    {"AROMA_ICON_VIEW_QUILT", "\ue8f1"},
    {"AROMA_ICON_VIEW_STREAM", "\ue8f2"},
    {"AROMA_ICON_VIEW_WEEK", "\ue8f3"},
    {"AROMA_ICON_VIGNETTE", "\ue435"},
    {"AROMA_ICON_VISIBILITY", "\ue8f4"},
    {"AROMA_ICON_VISIBILITY_OFF", "\ue8f5"},
    {"AROMA_ICON_VOICE_CHAT", "\ue62e"},
    {"AROMA_ICON_VOICEMAIL", "\ue0d9"},
    {"AROMA_ICON_VOLUME_DOWN", "\ue04d"},
    {"AROMA_ICON_VOLUME_MUTE", "\ue04e"},
    {"AROMA_ICON_VOLUME_OFF", "\ue04f"},
    {"AROMA_ICON_VOLUME_UP", "\ue050"},
    {"AROMA_ICON_VPN_KEY", "\ue0da"},
    {"AROMA_ICON_VPN_LOCK", "\ue62f"},
    {"AROMA_ICON_WALLPAPER", "\ue1bc"},
    {"AROMA_ICON_WARNING", "\ue002"},
    {"AROMA_ICON_WATCH", "\ue334"},
    {"AROMA_ICON_WATCH_LATER", "\ue924"},
    {"AROMA_ICON_WB_AUTO", "\ue42c"},
    {"AROMA_ICON_WB_CLOUDY", "\ue42d"},
    {"AROMA_ICON_WB_INCANDESCENT", "\ue42e"},
    {"AROMA_ICON_WB_IRIDESCENT", "\ue436"},
    {"AROMA_ICON_WB_SUNNY", "\ue430"},
    {"AROMA_ICON_WC", "\ue63d"},
    {"AROMA_ICON_WEB", "\ue051"},
    {"AROMA_ICON_WEB_ASSET", "\ue069"},
    {"AROMA_ICON_WEEKEND", "\ue16b"},
    {"AROMA_ICON_WHATSHOT", "\ue80e"},
    {"AROMA_ICON_WIDGETS", "\ue1bd"},
    {"AROMA_ICON_WIFI", "\ue63e"},
    {"AROMA_ICON_WIFI_LOCK", "\ue1e1"},
    {"AROMA_ICON_WIFI_TETHERING", "\ue1e2"},
    {"AROMA_ICON_WORK", "\ue8f9"},
    {"AROMA_ICON_WRAP_TEXT", "\ue25b"},
    {"AROMA_ICON_YOUTUBE_SEARCHED_FOR", "\ue8fa"},
    {"AROMA_ICON_ZOOM_IN", "\ue8ff"},
    {"AROMA_ICON_ZOOM_OUT", "\ue900"},
    {"AROMA_ICON_ZOOM_OUT_MAP", "\ue56b"},
    {NULL, NULL}
};

static const char *resolve_icon(const char *name)
{
    if (!name) return NULL;
    
    const char *clean_name = name;
    if (clean_name[0] == '"') {
        clean_name++;
        size_t len = strlen(clean_name);
        if (len > 0 && clean_name[len - 1] == '"') {
            char *unquoted = strdup(clean_name);
            unquoted[len - 1] = '\0';
            for (int i = 0; ICON_MAP[i].name; i++) {
                if (strcmp(ICON_MAP[i].name, unquoted) == 0) {
                    free(unquoted);
                    return ICON_MAP[i].codepoint;
                }
            }
            free(unquoted);
        }
    }
    
    for (int i = 0; ICON_MAP[i].name; i++) {
        if (strcmp(ICON_MAP[i].name, name) == 0)
            return ICON_MAP[i].codepoint;
    }
    
    return name;
}
static CallbackEntry s_callbacks[MAX_CALLBACKS];
static int s_callback_count = 0;

void IncenseRegisterCallback(const char *name, IncenseCallbackType type, void *fn, void *userdata)
{
    if (!name || !fn) return;
    for (int i = 0; i < s_callback_count; i++) {
        if (strcmp(s_callbacks[i].name, name) == 0) {
            s_callbacks[i].type = type;
            s_callbacks[i].fn = fn;
            s_callbacks[i].userdata = userdata;
            return;
        }
    }
    if (s_callback_count >= MAX_CALLBACKS) {
        fprintf(stderr, "incense: callback registry full, cannot register '%s'\n", name);
        return;
    }
    strncpy(s_callbacks[s_callback_count].name, name, sizeof(s_callbacks[0].name) - 1);
    s_callbacks[s_callback_count].name[sizeof(s_callbacks[0].name) - 1] = '\0';
    s_callbacks[s_callback_count].type = type;
    s_callbacks[s_callback_count].fn = fn;
    s_callbacks[s_callback_count].userdata = userdata;
    s_callback_count++;
}

void IncenseClearCallbacks(void)
{
    s_callback_count = 0;
}

static CallbackEntry *callback_find(const char *name)
{
    if (!name) return NULL;
    for (int i = 0; i < s_callback_count; i++)
        if (strcmp(s_callbacks[i].name, name) == 0)
            return &s_callbacks[i];
    return NULL;
}

static void registry_register(WidgetRegistry *reg, const char *id, AromaNode *node)
{
    if (!id || !node || reg->count >= MAX_NAMED_WIDGETS) return;
    for (int i = 0; i < reg->count; i++) {
        if (strcmp(reg->items[i].id, id) == 0) {
            reg->items[i].node = node;
            return;
        }
    }
    strncpy(reg->items[reg->count].id, id, sizeof(reg->items[0].id) - 1);
    reg->items[reg->count].id[sizeof(reg->items[0].id) - 1] = '\0';
    reg->items[reg->count].node = node;
    reg->count++;
}

static AromaNode *registry_find(const WidgetRegistry *reg, const char *id)
{
    if (!id) return NULL;
    for (int i = 0; i < reg->count; i++)
        if (strcmp(reg->items[i].id, id) == 0)
            return reg->items[i].node;
    return NULL;
}

static void props_collect(IncenseNode *node, PropBag *bag)
{
    bag->count = 0;
    IncenseNode *cur = node->first_child;
    while (cur && bag->count < MAX_PROPS) {
        if (cur->type == INCENSE_PROPERTY) {
            bag->items[bag->count].key = cur->name;
            bag->items[bag->count].value = cur->value;
            bag->count++;
        }
        cur = cur->next_sibling;
    }
}

static const char *props_get(const PropBag *bag, const char *key)
{
    for (int i = 0; i < bag->count; i++)
        if (strcmp(bag->items[i].key, key) == 0)
            return bag->items[i].value;
    return NULL;
}

static int props_int(const PropBag *bag, const char *key, int def)
{
    const char *v = props_get(bag, key);
    return v ? atoi(v) : def;
}

static float props_float(const PropBag *bag, const char *key, float def)
{
    const char *v = props_get(bag, key);
    return v ? (float)atof(v) : def;
}

static bool props_bool(const PropBag *bag, const char *key, bool def)
{
    const char *v = props_get(bag, key);
    if (!v) return def;
    return strcmp(v, "true") == 0;
}

static uint32_t props_color(const PropBag *bag, const char *key, uint32_t def)
{
    const char *v = props_get(bag, key);
    if (!v || v[0] != '#') return def;
    return (uint32_t)strtoul(v + 1, NULL, 16);
}

static char *props_str_dup(const PropBag *bag, const char *key, const char *def)
{
    const char *v = props_get(bag, key);
    const char *s = (v && v[0] == '"') ? v + 1 : (v ? v : def);
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len > 0 && s[len - 1] == '"') len--;
    char *out = malloc(len + 1);
    if (!out) return NULL;
    memcpy(out, s, len);
    out[len] = '\0';
    return out;
}

static CallbackEntry *resolve_callback(const PropBag *bag, const char *prop)
{
    char *name = props_str_dup(bag, prop, NULL);
    if (!name) return NULL;
    CallbackEntry *entry = callback_find(name);
    if (!entry)
        fprintf(stderr, "incense: callback \"%s\" referenced by %s= not registered\n", name, prop);
    free(name);
    return entry;
}

static AromaNode *resolve_parent(IncenseNode *node, AromaNode *structural_parent, const BuildCtx *ctx)
{
    IncenseNode *cur = node->first_child;
    while (cur) {
        if (cur->type == INCENSE_PROPERTY && strcmp(cur->name, "parent") == 0) {
            AromaNode *override = registry_find(ctx->registry, cur->value);
            if (override) return override;
            fprintf(stderr, "incense: parent \"%s\" not found in registry, falling back\n", cur->value);
            return structural_parent;
        }
        cur = cur->next_sibling;
    }
    return structural_parent;
}

static void maybe_register(const PropBag *bag, AromaNode *built, BuildCtx *ctx)
{
    if (!built) return;
    const char *id = props_get(bag, "id");
    if (!id) return;
    registry_register(ctx->registry, id, built);
}

static int collect_item_nodes(IncenseNode *node, const char *item_name, IncenseNode *out[], int max_out)
{
    int count = 0;
    IncenseNode *cur = node->first_child;
    while (cur && count < max_out) {
        if (cur->type == INCENSE_OBJECT && strcmp(cur->name, item_name) == 0)
            out[count++] = cur;
        cur = cur->next_sibling;
    }
    return count;
}

static bool is_property_node(IncenseNode *node)
{
    return node && node->type == INCENSE_PROPERTY;
}

static AromaNode *build_widget(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx);

static void build_children(IncenseNode *node, AromaNode *parent, BuildCtx *ctx)
{
    IncenseNode *child = node->first_child;
    while (child) {
        if (child->type == INCENSE_OBJECT && !is_property_node(child))
            build_widget(child, parent, ctx);
        child = child->next_sibling;
    }
}
static bool incense_button_click_bridge(AromaNode *node, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return false;
    if (entry->type == INCENSE_CALLBACK_BOOL_PTR) {
        bool (*cb)(AromaNode *, void *) = (bool (*)(AromaNode *, void *))entry->fn;
        return cb(node, entry->userdata);
    }
    if (entry->type == INCENSE_CALLBACK_VOID_PTR) {
        void (*cb)(void *) = (void (*)(void *))entry->fn;
        cb(entry->userdata);
        return true;
    }
    return false;
}
static void incense_fab_click_bridge(void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_VOID_PTR) {
        void (*cb)(void *) = (void (*)(void *))entry->fn;
        cb(entry->userdata);
    }
}

static void incense_dropdown_change_bridge(int index, const char *option, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_INT_STRING_PTR) {
        void (*cb)(int, const char *, void *) = (void (*)(int, const char *, void *))entry->fn;
        cb(index, option, entry->userdata);
    }
}

static void incense_checkbox_change_bridge(bool checked, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_BOOL_BOOL_PTR) {
        void (*cb)(bool, void *) = (void (*)(bool, void *))entry->fn;
        cb(checked, entry->userdata);
    }
}

static bool incense_switch_change_bridge(AromaNode *node, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return false;
    if (entry->type == INCENSE_CALLBACK_BOOL_PTR) {
        bool (*cb)(AromaNode *, void *) = (bool (*)(AromaNode *, void *))entry->fn;
        return cb(node, entry->userdata);
    }
    return false;
}

static bool incense_slider_change_bridge(AromaNode *node, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return false;
    if (entry->type == INCENSE_CALLBACK_BOOL_PTR) {
        bool (*cb)(AromaNode *, void *) = (bool (*)(AromaNode *, void *))entry->fn;
        return cb(node, entry->userdata);
    }
    return false;
}

static bool incense_textbox_change_bridge(AromaNode *node, const char *text, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return false;
    if (entry->type == INCENSE_CALLBACK_NODE_STRING_PTR) {
        bool (*cb)(AromaNode *, const char *, void *) = (bool (*)(AromaNode *, const char *, void *))entry->fn;
        return cb(node, text, entry->userdata);
    }
    return false;
}

static void incense_listview_select_bridge(int index, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_INT_PTR) {
        void (*cb)(int, void *) = (void (*)(int, void *))entry->fn;
        cb(index, entry->userdata);
    }
}

static void incense_menuitem_click_bridge(void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_VOID_PTR) {
        void (*cb)(void *) = (void (*)(void *))entry->fn;
        cb(entry->userdata);
    }
}

static void incense_sidebar_select_bridge(AromaNode *node, int index, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_NODE_INT_PTR) {
        void (*cb)(AromaNode *, int, void *) = (void (*)(AromaNode *, int, void *))entry->fn;
        cb(node, index, entry->userdata);
    }
}

static void incense_tabs_change_bridge(AromaNode *node, int index, void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_NODE_INT_PTR) {
        void (*cb)(AromaNode *, int, void *) = (void (*)(AromaNode *, int, void *))entry->fn;
        cb(node, index, entry->userdata);
    }
}
static AromaNode *build_button(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_click = resolve_callback(&bag, "on_click");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 120);
    int h = props_int(&bag, "height", 40);
    char *text = props_str_dup(&bag, "text", "Button");
    AromaNode *btn = aroma_ui_button(parent, text, x, y, w, h,
        on_click ? incense_button_click_bridge : NULL,
        on_click ? (void *)on_click : NULL,
        ctx->font);
    if (btn) node->id = btn->node_id;
    free(text);
    maybe_register(&bag, btn, ctx);
    build_children(node, btn, ctx);
    return btn;
}

static AromaNode *build_label(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    char *text = props_str_dup(&bag, "text", "");
    const char *style_str = props_get(&bag, "style");
    AromaLabelStyle style = LABEL_STYLE_LABEL_LARGE;
    if (style_str) {
        if (strcmp(style_str, "large") == 0) style = LABEL_STYLE_LABEL_LARGE;
        else if (strcmp(style_str, "medium") == 0) style = LABEL_STYLE_LABEL_MEDIUM;
        else if (strcmp(style_str, "small") == 0) style = LABEL_STYLE_LABEL_SMALL;
    }
    AromaNode *lbl = aroma_ui_label(parent, text, x, y, style, ctx->font);
    if (lbl) node->id = lbl->node_id;
    free(text);
    maybe_register(&bag, lbl, ctx);
    build_children(node, lbl, ctx);
    return lbl;
}static AromaNode *build_container(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 200);
    const char *layout_str = props_get(&bag, "layout");
    AromaLayoutMode layout = AROMA_LAYOUT_MODE_NONE;
    if (layout_str && strcmp(layout_str, "flex") == 0) layout = AROMA_LAYOUT_MODE_FLEX;
    const char *dir_str = props_get(&bag, "direction");
    AromaFlexDirection dir = AROMA_FLEX_COLUMN;
    if (dir_str && strcmp(dir_str, "row") == 0) dir = AROMA_FLEX_ROW;
    AromaNode *cont = aroma_ui_container(parent, x, y, w, h, layout, dir, AROMA_JUSTIFY_START, AROMA_ALIGN_START);
    if (cont) node->id = cont->node_id;
    maybe_register(&bag, cont, ctx);
    build_children(node, cont, ctx);
    return cont;
}
static AromaNode *build_scrollview(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 200);
    const char *dir_str = props_get(&bag, "direction");
    AromaScrollDirection dir = AROMA_SCROLL_VERTICAL;
    if (dir_str) {
        if (strcmp(dir_str, "horizontal") == 0) dir = AROMA_SCROLL_HORIZONTAL;
        else if (strcmp(dir_str, "both") == 0) dir = AROMA_SCROLL_BOTH;
    }
    
    AromaNode *sv = aroma_container_create(parent, x, y, w, h);
    if (!sv) return NULL;
    
    node->id = sv->node_id;
    
    aroma_container_set_scrollable(sv, true);
    aroma_container_set_scroll_direction(sv, dir);
    
    maybe_register(&bag, sv, ctx);
    build_children(node, sv, ctx);
    
    aroma_container_update_auto_content_size(sv);
    
    return sv;
}
static AromaNode *build_checkbox(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 160);
    int h = props_int(&bag, "height", 32);
    char *label = props_str_dup(&bag, "label", "");
    AromaNode *cb = aroma_ui_checkbox(parent, label, x, y, w, h,
        on_change ? incense_checkbox_change_bridge : NULL,
        on_change ? (void *)on_change : NULL,
        ctx->font);
    if (cb) node->id = cb->node_id;
    free(label);
    maybe_register(&bag, cb, ctx);
    build_children(node, cb, ctx);
    return cb;
}

static AromaNode *build_switch(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 56);
    int h = props_int(&bag, "height", 28);
    bool state = props_bool(&bag, "value", false);
    AromaNode *sw = aroma_ui_switch(parent, x, y, w, h, state,
        on_change ? incense_switch_change_bridge : NULL,
        on_change ? (void *)on_change : NULL);
    if (sw) node->id = sw->node_id;
    maybe_register(&bag, sw, ctx);
    build_children(node, sw, ctx);
    return sw;
}

static AromaNode *build_slider(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 24);
    int mn = props_int(&bag, "min", 0);
    int mx = props_int(&bag, "max", 100);
    int val = props_int(&bag, "value", 0);
    AromaNode *sl = aroma_ui_slider(parent, x, y, w, h, mn, mx, val,
        on_change ? incense_slider_change_bridge : NULL,
        on_change ? (void *)on_change : NULL);
    if (sl) node->id = sl->node_id;
    maybe_register(&bag, sl, ctx);
    build_children(node, sl, ctx);
    return sl;
}

static AromaNode *build_textbox(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    CallbackEntry *on_submit = resolve_callback(&bag, "on_submit");
    CallbackEntry *cb = on_change ? on_change : on_submit;
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 36);
    char *ph = props_str_dup(&bag, "placeholder", "");
    AromaNode *tb = aroma_ui_textbox(parent, x, y, w, h, ph,
        cb ? incense_textbox_change_bridge : NULL,
        cb ? (void *)cb : NULL,
        ctx->font);
    if (tb) node->id = tb->node_id;
    free(ph);
    maybe_register(&bag, tb, ctx);
    build_children(node, tb, ctx);
    return tb;
}static AromaNode *build_progressbar(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 8);
    float prog = props_float(&bag, "value", 0.0f);
    const char *type_str = props_get(&bag, "type");
    AromaProgressType type = PROGRESS_TYPE_DETERMINATE;
    if (type_str && strcmp(type_str, "indeterminate") == 0) type = PROGRESS_TYPE_INDETERMINATE;
    AromaNode *pb = aroma_ui_progressbar(parent, x, y, w, h, type, prog);
    if (pb) node->id = pb->node_id;
    maybe_register(&bag, pb, ctx);
    build_children(node, pb, ctx);
    return pb;
}

static AromaNode *build_divider(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int length = props_int(&bag, "length", 100);
    const char *or_str = props_get(&bag, "orientation");
    AromaDividerOrientation orient = DIVIDER_ORIENTATION_HORIZONTAL;
    if (or_str && strcmp(or_str, "vertical") == 0) orient = DIVIDER_ORIENTATION_VERTICAL;
    AromaNode *dv = aroma_ui_divider(parent, x, y, length, orient);
    if (dv) node->id = dv->node_id;
    maybe_register(&bag, dv, ctx);
    build_children(node, dv, ctx);
    return dv;
}

static AromaNode *build_card(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 120);
    const char *type_str = props_get(&bag, "type");
    AromaCardType type = CARD_TYPE_ELEVATED;
    if (type_str) {
        if (strcmp(type_str, "outlined") == 0) type = CARD_TYPE_OUTLINED;
        else if (strcmp(type_str, "filled") == 0) type = CARD_TYPE_FILLED;
    }
    AromaNode *card = aroma_ui_card(parent, x, y, w, h, type);
    if (card) node->id = card->node_id;
    maybe_register(&bag, card, ctx);
    build_children(node, card, ctx);
    return card;
}
static AromaNode *build_fab(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_click = resolve_callback(&bag, "on_click");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    const char *icon_raw = props_get(&bag, "icon");
    const char *icon_resolved = resolve_icon(icon_raw);
    char *icon = icon_resolved ? strdup(icon_resolved) : props_str_dup(&bag, "icon", "+");
    const char *size_str = props_get(&bag, "size");
    AromaFABSize size = FAB_SIZE_NORMAL;
    if (size_str) {
        if (strcmp(size_str, "small") == 0) size = FAB_SIZE_SMALL;
        else if (strcmp(size_str, "large") == 0) size = FAB_SIZE_LARGE;
    }
    AromaNode *fab = aroma_ui_fab(parent, x, y, size, icon,
        on_click ? incense_fab_click_bridge : NULL,
        on_click ? (void *)on_click : NULL,
        ctx->icon_font ? ctx->icon_font : ctx->font);
    if (fab) node->id = fab->node_id;
    free(icon);
    maybe_register(&bag, fab, ctx);
    build_children(node, fab, ctx);
    return fab;
}
static void incense_iconbutton_click_bridge(void *user_data)
{
    CallbackEntry *entry = (CallbackEntry *)user_data;
    if (!entry || !entry->fn) return;
    if (entry->type == INCENSE_CALLBACK_VOID_PTR) {
        void (*cb)(void *) = (void (*)(void *))entry->fn;
        cb(entry->userdata);
    }
}static AromaNode *build_iconbutton(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_click = resolve_callback(&bag, "on_click");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int size = props_int(&bag, "size", 40);
    const char *icon_raw = props_get(&bag, "icon");
    const char *icon_resolved = resolve_icon(icon_raw);
    char *icon = icon_resolved ? strdup(icon_resolved) : props_str_dup(&bag, "icon", "");
    const char *var_str = props_get(&bag, "variant");
    AromaIconButtonVariant variant = ICON_BUTTON_STANDARD;
    if (var_str) {
        if (strcmp(var_str, "filled") == 0) variant = ICON_BUTTON_FILLED;
        else if (strcmp(var_str, "tonal") == 0) variant = ICON_BUTTON_TONAL;
        else if (strcmp(var_str, "outlined") == 0) variant = ICON_BUTTON_OUTLINED;
    }
    AromaNode *btn = aroma_ui_iconbutton(parent, icon, x, y, size, variant,
        on_click ? incense_iconbutton_click_bridge : NULL,
        on_click ? (void *)on_click : NULL,
        ctx->icon_font ? ctx->icon_font : ctx->font);
    if (btn) node->id = btn->node_id;
    free(icon);
    maybe_register(&bag, btn, ctx);
    build_children(node, btn, ctx);
    return btn;
}
static AromaNode *build_icon(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int size = props_int(&bag, "size", 24);
    AromaNode *icon = aroma_icon_create(parent, x, y, size);
    if (icon) {
        node->id = icon->node_id;
        const char *text_raw = props_get(&bag, "text");
        const char *image = props_get(&bag, "src");
        if (text_raw) {
            const char *resolved = resolve_icon(text_raw);
            const char *final_text = resolved ? resolved : text_raw;
            char *text_dup = strdup(final_text);
            aroma_icon_set_text(icon, text_dup, ctx->icon_font ? ctx->icon_font : ctx->font);
            free(text_dup);
        } else if (image) {
            char *image_dup = props_str_dup(&bag, "src", "");
            aroma_icon_set_image(icon, image_dup);
            free(image_dup);
        }
        const char *color_str = props_get(&bag, "color");
        if (color_str) aroma_icon_set_color(icon, props_color(&bag, "color", 0x000000FF));
    }
    maybe_register(&bag, icon, ctx);
    build_children(node, icon, ctx);
    return icon;
}
static AromaNode *build_snackbar(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int dur = props_int(&bag, "duration", 3000);
    char *msg = props_str_dup(&bag, "message", "");
    AromaNode *snk = aroma_ui_snackbar(parent, msg, dur, ctx->font);
    if (snk) node->id = snk->node_id;
    free(msg);
    maybe_register(&bag, snk, ctx);
    build_children(node, snk, ctx);
    return snk;
}

static AromaNode *build_listview(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_select = resolve_callback(&bag, "on_select");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 300);
    AromaNode *lv = aroma_ui_listview(parent, x, y, w, h,
        on_select ? incense_listview_select_bridge : NULL,
        on_select ? (void *)on_select : NULL,
        ctx->font);
    if (lv) node->id = lv->node_id;
    maybe_register(&bag, lv, ctx);
    build_children(node, lv, ctx);
    return lv;
}

static AromaNode *build_dialog(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int w = props_int(&bag, "width", 320);
    int h = props_int(&bag, "height", 200);
    char *title = props_str_dup(&bag, "title", "Dialog");
    char *msg = props_str_dup(&bag, "message", "");
    const char *type_str = props_get(&bag, "type");
    AromaDialogType type = DIALOG_TYPE_BASIC;
    if (type_str && strcmp(type_str, "fullscreen") == 0) type = DIALOG_TYPE_FULL_SCREEN;
    AromaNode *dlg = aroma_ui_dialog(parent, title, msg, w, h, type, ctx->font);
    if (dlg) node->id = dlg->node_id;
    free(title);
    free(msg);
    maybe_register(&bag, dlg, ctx);
    build_children(node, dlg, ctx);
    return dlg;
}

static AromaNode *build_image(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 100);
    int h = props_int(&bag, "height", 100);
    char *path = props_str_dup(&bag, "src", "");
    AromaNode *img = aroma_ui_image(parent, path, x, y, w, h);
    if (img) node->id = img->node_id;
    free(path);
    maybe_register(&bag, img, ctx);
    build_children(node, img, ctx);
    return img;
}

static AromaNode *build_canvas(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 200);
    AromaNode *cv = aroma_canvas_create(parent, x, y, w, h);
    if (cv) node->id = cv->node_id;
    maybe_register(&bag, cv, ctx);
    build_children(node, cv, ctx);
    return cv;
}

static AromaNode *build_debugoverlay(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    bool visible = props_bool(&bag, "visible", true);
    AromaNode *ov = aroma_debug_overlay_create(parent, x, y, w);
    if (ov) {
        node->id = ov->node_id;
        aroma_debug_overlay_set_font(ov, ctx->font);
        aroma_debug_overlay_set_visible(ov, visible);
    }
    maybe_register(&bag, ov, ctx);
    build_children(node, ov, ctx);
    return ov;
}

static AromaNode *build_dropdown(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 36);
    AromaNode *dd = aroma_dropdown_create(parent, x, y, w, h);
    if (!dd) return NULL;
    node->id = dd->node_id;
    IncenseNode *items[MAX_ITEM_NODES];
    int n = collect_item_nodes(node, "Option", items, MAX_ITEM_NODES);
    for (int i = 0; i < n; i++) {
        PropBag obag;
        props_collect(items[i], &obag);
        char *text = props_str_dup(&obag, "text", "");
        aroma_dropdown_add_option(dd, text);
        free(text);
    }
    if (on_change) aroma_dropdown_set_on_change(dd, incense_dropdown_change_bridge, on_change);
    aroma_dropdown_setup_events(dd, NULL, NULL);
    aroma_dropdown_set_font(dd, ctx->font);
    maybe_register(&bag, dd, ctx);
    build_children(node, dd, ctx);
    return dd;
}

static AromaNode *build_gif(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 100);
    int h = props_int(&bag, "height", 100);
    char *path = props_str_dup(&bag, "src", "");
    AromaNode *gif = aroma_gif_create(parent, path, x, y, w, h);
    if (gif) {
        node->id = gif->node_id;
        if (props_bool(&bag, "autoplay", true)) aroma_gif_play(gif);
    }
    free(path);
    maybe_register(&bag, gif, ctx);
    build_children(node, gif, ctx);
    return gif;
}

static AromaNode *build_loading(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int radius = props_int(&bag, "radius", 16);
    int thickness = props_int(&bag, "thickness", 3);
    uint32_t color = props_color(&bag, "color", 0x000000FF);
    AromaNode *ld = aroma_loading_create(parent, x, y, radius, thickness, color);
    if (ld) node->id = ld->node_id;
    maybe_register(&bag, ld, ctx);
    build_children(node, ld, ctx);
    return ld;
}

static AromaNode *build_map(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 300);
    int h = props_int(&bag, "height", 300);
    AromaNode *map = aroma_map_create(parent, x, y, w, h);
    if (!map) return NULL;
    node->id = map->node_id;
    double lat = (double)props_float(&bag, "lat", 0.0f);
    double lon = (double)props_float(&bag, "lon", 0.0f);
    int zoom = props_int(&bag, "zoom", 4);
    bool attribution = props_bool(&bag, "attribution", true);
    aroma_map_set_center(map, lat, lon);
    aroma_map_set_zoom(map, zoom);
    aroma_map_set_show_attribution(map, attribution);
    IncenseNode *items[MAX_ITEM_NODES];
    int n = collect_item_nodes(node, "Marker", items, MAX_ITEM_NODES);
    for (int i = 0; i < n; i++) {
        PropBag mbag;
        props_collect(items[i], &mbag);
        double mlat = (double)props_float(&mbag, "lat", 0.0f);
        double mlon = (double)props_float(&mbag, "lon", 0.0f);
        uint32_t mcolor = props_color(&mbag, "color", 0xFF0000FF);
        const char *icon = props_get(&mbag, "icon");
        const char *popup = props_get(&mbag, "popup");
        if (popup) {
            char *popup_dup = props_str_dup(&mbag, "popup", "");
            aroma_map_add_popup_marker(map, mlat, mlon, mcolor, popup_dup);
            free(popup_dup);
        } else if (icon) {
            char *icon_dup = props_str_dup(&mbag, "icon", "");
            aroma_map_add_icon_marker(map, mlat, mlon, mcolor, icon_dup);
            free(icon_dup);
        } else {
            aroma_map_add_marker(map, mlat, mlon, mcolor);
        }
    }
    maybe_register(&bag, map, ctx);
    build_children(node, map, ctx);
    return map;
}

static AromaNode *build_menu(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    AromaNode *menu = aroma_menu_create(parent, x, y);
    if (!menu) return NULL;
    node->id = menu->node_id;
    aroma_menu_set_font(menu, ctx->font);
    aroma_menu_set_icon_font(menu, ctx->icon_font ? ctx->icon_font : ctx->font);
    IncenseNode *cur = node->first_child;
    while (cur) {
        if (cur->type == INCENSE_OBJECT && strcmp(cur->name, "MenuItem") == 0) {
            PropBag ibag;
            props_collect(cur, &ibag);
            char *text = props_str_dup(&ibag, "text", "");
            const char *icon_raw = props_get(&ibag, "icon");
            const char *icon_resolved = resolve_icon(icon_raw);
            CallbackEntry *on_click = resolve_callback(&ibag, "on_click");
            if (icon_resolved) {
                char *icon_dup = strdup(icon_resolved);
                aroma_menu_add_item_with_icon(menu, text, icon_dup,
                    on_click ? incense_menuitem_click_bridge : NULL,
                    on_click ? (void *)on_click : NULL);
                free(icon_dup);
            } else {
                aroma_menu_add_item(menu, text,
                    on_click ? incense_menuitem_click_bridge : NULL,
                    on_click ? (void *)on_click : NULL);
            }
            free(text);
        } else if (cur->type == INCENSE_OBJECT && strcmp(cur->name, "Separator") == 0) {
            aroma_menu_add_separator(menu);
        }
        cur = cur->next_sibling;
    }
    maybe_register(&bag, menu, ctx);
    build_children(node, menu, ctx);
    return menu;
}

static AromaNode *build_radiobutton(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_click = resolve_callback(&bag, "on_click");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 160);
    int h = props_int(&bag, "height", 32);
    int group_id = props_int(&bag, "group", 0);
    char *label = props_str_dup(&bag, "label", "");
    AromaNode *rb = aroma_radiobutton_create(parent, label, x, y, w, h, group_id);
    if (rb) {
        node->id = rb->node_id;
        aroma_radiobutton_set_font(rb, ctx->font);
        if (props_bool(&bag, "selected", false)) aroma_radiobutton_set_selected(rb, true);
        if (on_click) aroma_radiobutton_set_callback(rb, incense_menuitem_click_bridge, on_click);
    }
    free(label);
    maybe_register(&bag, rb, ctx);
    build_children(node, rb, ctx);
    return rb;
}static AromaNode *build_sidebar(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_select = resolve_callback(&bag, "on_select");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 200);
    int h = props_int(&bag, "height", 400);
    IncenseNode *items[AROMA_SIDEBAR_MAX_ITEMS];
    int n = collect_item_nodes(node, "Item", items, AROMA_SIDEBAR_MAX_ITEMS);
    char *label_bufs[AROMA_SIDEBAR_MAX_ITEMS];
    const char *labels[AROMA_SIDEBAR_MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        PropBag ibag;
        props_collect(items[i], &ibag);
        label_bufs[i] = props_str_dup(&ibag, "text", "");
        labels[i] = label_bufs[i] ? label_bufs[i] : "";
    }
    AromaNode *sb = aroma_sidebar_create(parent, x, y, w, h, labels, n);
    for (int i = 0; i < n; i++) free(label_bufs[i]);
    if (!sb) return NULL;
    node->id = sb->node_id;
    aroma_sidebar_set_font(sb, ctx->font);
    for (int i = 0; i < n; i++) {
        PropBag ibag;
        props_collect(items[i], &ibag);
        const char *icon_raw = props_get(&ibag, "icon");
        if (icon_raw) {
            const char *icon_resolved = resolve_icon(icon_raw);
            char *icon_dup = icon_resolved ? strdup(icon_resolved) : props_str_dup(&ibag, "icon", "");
            aroma_sidebar_set_icon(sb, i, icon_dup, ctx->icon_font ? ctx->icon_font : ctx->font);
            free(icon_dup);
        }
    }
    for (int i = 0; i < n; i++) {
        IncenseNode *content_nodes[MAX_CHILDREN];
        int content_count = 0;
        IncenseNode *child = items[i]->first_child;
        while (child && content_count < MAX_CHILDREN) {
            if (child->type == INCENSE_OBJECT && !is_property_node(child))
                content_nodes[content_count++] = child;
            child = child->next_sibling;
        }
        if (content_count > 0) {
            AromaNode *built_children[MAX_CHILDREN];
            int built_count = 0;
            for (int j = 0; j < content_count && built_count < MAX_CHILDREN; j++) {
                AromaNode *built = build_widget(content_nodes[j], structural_parent, ctx);
                if (built) built_children[built_count++] = built;
            }
            if (built_count > 0) {
                aroma_sidebar_set_content(sb, i, built_children, built_count);
            }
        }
    }
    if (on_select) aroma_sidebar_set_on_select(sb, incense_sidebar_select_bridge, on_select);
    maybe_register(&bag, sb, ctx);
    return sb;
}

static AromaNode *build_table(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 400);
    int h = props_int(&bag, "height", 300);
    IncenseNode *columns[MAX_ITEM_NODES];
    int num_cols = collect_item_nodes(node, "Column", columns, MAX_ITEM_NODES);
    if (num_cols == 0) num_cols = props_int(&bag, "columns", 1);
    AromaNode *table = aroma_table_create(parent, x, y, w, h, num_cols);
    if (!table) return NULL;
    node->id = table->node_id;
    aroma_table_set_font(table, ctx->font);
    for (int i = 0; i < num_cols; i++) {
        PropBag cbag;
        props_collect(columns[i], &cbag);
        char *header = props_str_dup(&cbag, "header", "");
        aroma_table_set_header(table, i, header);
        free(header);
        int col_width = props_int(&cbag, "width", 0);
        if (col_width > 0) aroma_table_set_col_width(table, i, col_width);
    }
    maybe_register(&bag, table, ctx);
    build_children(node, table, ctx);
    return table;
}static AromaNode *build_tabs(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_change = resolve_callback(&bag, "on_change");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    int w = props_int(&bag, "width", 400);
    int h = props_int(&bag, "height", 48);

    IncenseNode *items[AROMA_TABS_MAX];
    int n = collect_item_nodes(node, "Tab", items, AROMA_TABS_MAX);

    char *label_bufs[AROMA_TABS_MAX];
    const char *labels[AROMA_TABS_MAX];
    for (int i = 0; i < n; i++) {
        PropBag ibag;
        props_collect(items[i], &ibag);
        label_bufs[i] = props_str_dup(&ibag, "text", "");
        labels[i] = label_bufs[i] ? label_bufs[i] : "";
    }

    AromaNode *tabs = aroma_ui_tabs(parent, x, y, w, h, labels, n,
        on_change ? incense_tabs_change_bridge : NULL,
        on_change ? (void *)on_change : NULL,
        ctx->font);

    for (int i = 0; i < n; i++) free(label_bufs[i]);

    if (!tabs) return NULL;
    node->id = tabs->node_id;
    aroma_tabs_set_font(tabs, ctx->font);

    for (int i = 0; i < n; i++) {
        PropBag ibag;
        props_collect(items[i], &ibag);
        const char *icon_raw = props_get(&ibag, "icon");
        if (icon_raw) {
            const char *icon_resolved = resolve_icon(icon_raw);
            char *icon_dup = icon_resolved ? strdup(icon_resolved) : props_str_dup(&ibag, "icon", "");
            aroma_tabs_set_icon(tabs, i, icon_dup, ctx->icon_font ? ctx->icon_font : ctx->font);
            free(icon_dup);
        }
    }

    for (int i = 0; i < n; i++) {
        IncenseNode *content_nodes[MAX_CHILDREN];
        int content_count = 0;
        IncenseNode *child = items[i]->first_child;
        while (child && content_count < MAX_CHILDREN) {
            if (child->type == INCENSE_OBJECT && !is_property_node(child))
                content_nodes[content_count++] = child;
            child = child->next_sibling;
        }
        if (content_count > 0) {
            AromaNode *built_children[MAX_CHILDREN];
            int built_count = 0;
            for (int j = 0; j < content_count && built_count < MAX_CHILDREN; j++) {
                AromaNode *built = build_widget(content_nodes[j], structural_parent, ctx);
                if (built) built_children[built_count++] = built;
            }
            if (built_count > 0) {
                aroma_tabs_set_content(tabs, i, built_children, built_count);
            }
        }
    }

    maybe_register(&bag, tabs, ctx);
    return tabs;
}
static AromaNode *build_tooltip(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    char *text = props_str_dup(&bag, "text", "");
    const char *pos_str = props_get(&bag, "position");
    AromaTooltipPosition position = TOOLTIP_POSITION_TOP;
    if (pos_str) {
        if (strcmp(pos_str, "bottom") == 0) position = TOOLTIP_POSITION_BOTTOM;
        else if (strcmp(pos_str, "left") == 0) position = TOOLTIP_POSITION_LEFT;
        else if (strcmp(pos_str, "right") == 0) position = TOOLTIP_POSITION_RIGHT;
    }
    AromaNode *tt = aroma_tooltip_create(parent, text, x, y, position);
    if (tt) {
        node->id = tt->node_id;
        aroma_tooltip_set_font(tt, ctx->font);
    }
    free(text);
    maybe_register(&bag, tt, ctx);
    build_children(node, tt, ctx);
    return tt;
}
static AromaNode *build_chip(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    PropBag bag;
    props_collect(node, &bag);
    AromaNode *parent = resolve_parent(node, structural_parent, ctx);
    CallbackEntry *on_click = resolve_callback(&bag, "on_click");
    int x = props_int(&bag, "x", 0);
    int y = props_int(&bag, "y", 0);
    char *label = props_str_dup(&bag, "label", "Chip");
    (void)parent;
    (void)on_click;
    (void)x;
    (void)y;
    free(label);
    return NULL;
}
static const WidgetEntry WIDGET_TABLE[] = {
    { "Button", build_button },
    { "Label", build_label },
    { "Container", build_container },
    { "ScrollView", build_scrollview },
    { "Checkbox", build_checkbox },
    { "Switch", build_switch },
    { "Slider", build_slider },
    { "Textbox", build_textbox },
    { "ProgressBar", build_progressbar },
    { "Divider", build_divider },
    { "Card", build_card },
    { "Chip", build_chip },
    { "FAB", build_fab },
    { "IconButton", build_iconbutton },
    { "Snackbar", build_snackbar },
    { "ListView", build_listview },
    { "Dialog", build_dialog },
    { "Image", build_image },
    { "Canvas", build_canvas },
    { "DebugOverlay", build_debugoverlay },
    { "Dropdown", build_dropdown },
    { "GIF", build_gif },
    { "Icon", build_icon },
    { "Loading", build_loading },
    { "Map", build_map },
    { "Menu", build_menu },
    { "RadioButton", build_radiobutton },
    { "Sidebar", build_sidebar },
    { "Table", build_table },
    { "Tabs", build_tabs },
    { "Tooltip", build_tooltip },
    { NULL, NULL }
};

static AromaNode *build_widget(IncenseNode *node, AromaNode *structural_parent, BuildCtx *ctx)
{
    if (!node || node->type != INCENSE_OBJECT) return NULL;
    for (int i = 0; WIDGET_TABLE[i].name; i++) {
        if (strcmp(node->name, WIDGET_TABLE[i].name) == 0)
            return WIDGET_TABLE[i].build(node, structural_parent, ctx);
    }
    fprintf(stderr, "incense: unknown widget type '%s' (ignored)\n", node->name);
    return NULL;
}

AromaWindow *IncenseLoad(const IncenseDocument *doc, AromaFont *font, AromaFont *icon_font)
{
    if (!doc || !doc->root) return NULL;
    IncenseNode *root = doc->root;
    if (strcmp(root->name, "Window") != 0) {
        fprintf(stderr, "incense: root object must be 'Window', got '%s'\n", root->name);
        return NULL;
    }
    PropBag bag;
    props_collect(root, &bag);
    int w = props_int(&bag, "width", 800);
    int h = props_int(&bag, "height", 600);
    char *title = props_str_dup(&bag, "title", "Incense App");
    AromaWindow *window = aroma_ui_create_window(title, w, h);
    free(title);
    if (!window) return NULL;
    AromaNode *root_node = (AromaNode *)window;
    WidgetRegistry registry = { .count = 0 };
    BuildCtx ctx = { .registry = &registry, .font = font, .icon_font = icon_font };
    IncenseNode *child = root->first_child;
    while (child) {
        if (child->type == INCENSE_OBJECT) build_widget(child, root_node, &ctx);
        child = child->next_sibling;
    }
    return window;
}

AromaWindow *IncenseLoadFile(const char *path, AromaFont *font, AromaFont *icon_font)
{
    IncenseDocument *doc = IncenseParseFile(path);
    if (!doc) return NULL;
    AromaWindow *win = IncenseLoad(doc, font, icon_font);
    IncenseDestroy(doc);
    return win;
}

AromaWindow *IncenseLoadString(const char *source, AromaFont *font, AromaFont *icon_font)
{
    IncenseDocument *doc = IncenseParseString(source);
    if (!doc) return NULL;
    AromaWindow *win = IncenseLoad(doc, font, icon_font);
    IncenseDestroy(doc);
    return win;
}