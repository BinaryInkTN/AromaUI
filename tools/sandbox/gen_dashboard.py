#!/usr/bin/env python3
"""Generate the Loud analytics dashboard `dashboard_tablet` Incense example.

The example is a small, working application rather than a static mock:
every control in docs/sandbox.html maps to a callback implemented in
examples/16_loud_dashboard/src/loud_dashboard.c.  The markup stays
data-driven so the C side can update labels, charts, rows and overlays in
place.

Two outputs come from this one source of truth:

    # the example app: a small root plus one file per screen section
    python3 tools/sandbox/gen_dashboard.py --split examples/16_loud_dashboard/ui

    # the flattened markup embedded into the docs live demo
    python3 tools/sandbox/gen_dashboard.py > /tmp/dashboard_markup.txt

    # refresh that docs demo in place (splices the flat markup into the
    # 'dashboard_tablet' entry of docs/sandbox.html)
    python3 tools/sandbox/gen_dashboard.py --update-docs

The example tree uses Incense `@embed` includes with inline props
(`{ name: "value" }`, referenced as `embed_name`) so repeated widgets are
authored once.  The docs embed needs a single self-contained source string,
so the flat mode inlines the same fragments in Python.

The window is authored at 1280x800, matching the docs tablet frame.
"""
import argparse
import os
import re
import sys

__all__ = ["main"]

# ---------------------------------------------------------------- fragments

out = []
FRAGMENTS = {}
ROOT_TOKENS = []

EMBED_REF_RE = re.compile(r"embed_([A-Za-z0-9_]{1,63})")


class GenError(Exception):
    pass


class Embed:
    """An `@embed "path" { props }` include in the generated markup."""

    def __init__(self, path, props=None, indent="        "):
        self.path = path
        self.props = {k: str(v) for k, v in (props or {}).items()}
        self.indent = indent


class Fragment:
    """Capture the lines emitted inside `with Fragment(path): ...`."""

    def __init__(self, path):
        self.path = path
        self.lines = []
        self._saved = None

    def __enter__(self):
        self._saved = out[:]
        out.clear()
        return self

    def __exit__(self, *exc):
        self.lines = out[:]
        out.clear()
        out.extend(self._saved)
        FRAGMENTS[self.path] = self


def embed(path, indent="        ", **props):
    out.append(Embed(path, props, indent))


class Group:
    """A top-level Container.  Children keep absolute window coordinates."""

    def __init__(self, name, ident=None, visible=True):
        self.name = name
        self.ident = ident
        self.visible = visible
        self._saved = None

    def __enter__(self):
        self._saved = out[:]
        out.clear()
        return self

    def __exit__(self, *exc):
        body = out[:]
        out.clear()
        out.extend(self._saved)
        container(self.ident, 0, 0, 1280, 800, body, visible=self.visible)


class Sub:
    """A nested Container used to show/hide a small group of nodes at once."""

    def __init__(self, ident=None, visible=True):
        self.ident = ident
        self.visible = visible
        self._saved = None

    def __enter__(self):
        self._saved = out[:]
        out.clear()
        return self

    def __exit__(self, *exc):
        body = out[:]
        out.clear()
        out.extend(self._saved)
        container(self.ident, 0, 0, 1280, 800, body, visible=self.visible)


_section_stack = []


def begin_section():
    """Collect the following widgets into one nested container.

    AROMA_MAX_CHILD_NODES is 128, so large pages are split into one nested
    container per visual section.  Keeping the authoring order flat makes the
    generator readable while the emitted Incense tree stays shallow.
    """
    _section_stack.append(out[:])
    out.clear()


def end_section(ident=None):
    body = out[:]
    out.clear()
    out.extend(_section_stack.pop())
    container(ident, 0, 0, 1280, 800, body)


def container(ident, x, y, w, h, body, visible=True, z=None):
    add("        Container {")
    if ident:
        prop("id", f'"{ident}"')
    if z is not None:
        prop("z_index", z)
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    if not visible:
        prop("visible", 0)
    out.extend(body)
    add("        }")


def add(s=""):
    out.append(s)


def prop(name, value):
    add(f"            {name}: {value}")


def card(x, y, w, h, color, radius=22, z=2, kind="filled", ident=None,
         border=None, interactive=False, visible=True):
    add("        Card {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    if not visible:
        prop("visible", 0)
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    prop("type", kind)
    prop("color", f"#{color}")
    if border:
        prop("border_color", f"#{border}")
    if radius is not None:
        prop("corner_radius", radius)
    if interactive:
        prop("interactive", "true")
    add("        }")


def label(text, x, y, size, color="FFFFFF", z=4, ident=None, visible=True):
    add("        Label {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    if not visible:
        prop("visible", 0)
    prop("text", f'"{text}"')
    prop("size", size)
    prop("color", f"#{color}")
    prop("x", x)
    prop("y", y)
    add("        }")


def icon(name, x, y, size, color="FFFFFF", z=4, ident=None):
    add("        Icon {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("size", size)
    prop("icon_size", size)
    prop("text", f'"AROMA_ICON_{name}"')
    prop("color", f"#{color}")
    add("        }")


def icon_button(name, x, y, size, icon_size, bg, fg, cb=None, z=5, ident=None):
    add("        IconButton {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("size", size)
    prop("icon_size", icon_size)
    prop("icon", f'"AROMA_ICON_{name}"')
    prop("variant", "filled")
    prop("color", f"#{bg}")
    prop("icon_color", f"#{fg}")
    if cb:
        prop("on_click", f'"{cb}"')
    add("        }")


def image(src, x, y, w, h, radius=None, z=4, ident=None):
    add("        Image {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    prop("src", f'"{src}"')
    prop("scale", "cover")
    if radius is not None:
        prop("corner_radius", radius)
    add("        }")


def hit(cb, x, y, w, h, z=20, ident=None):
    add("        Button {")
    prop("z_index", z)
    if ident:
        prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    prop("text", '""')
    prop("type", "text")
    if cb:
        prop("on_click", f'"{cb}"')
    add("        }")


def textbox(ident, x, y, w, h, placeholder, cb=None, z=60):
    add("        Textbox {")
    prop("z_index", z)
    prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    prop("placeholder", f'"{placeholder}"')
    if cb:
        prop("on_change", f'"{cb}"')
    add("        }")


def switch(ident, x, y, value, cb, z=60, w=50, h=28, raw_value=None):
    add("        Switch {")
    prop("z_index", z)
    prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    if raw_value is not None:
        prop("value", raw_value)
    else:
        prop("value", "true" if value else "false")
    prop("on_change", f'"{cb}"')
    add("        }")


def chart(ident, x, y, w, h, data, color, thickness, z=4):
    add("        Chart {")
    prop("z_index", z)
    prop("id", f'"{ident}"')
    prop("x", x)
    prop("y", y)
    prop("width", w)
    prop("height", h)
    prop("size", 12)
    prop("data", f'"{",".join(str(v) for v in data)}"')
    prop("x_min", 0)
    prop("x_max", len(data) - 1)
    prop("y_min", 0)
    prop("y_max", 100)
    prop("line_color", f"#{color}")
    prop("line_thickness", thickness)
    prop("pad_left", 0)
    prop("pad_top", 0)
    prop("pad_right", 0)
    prop("pad_bottom", 0)
    prop("grid", 0)
    prop("frame", 0)
    add("        }")


def pill(text, x, y, w, h, bg, fg, cb, ident=None, text_size=14, z=30,
         text_x=None):
    card(x, y, w, h, bg, radius=h // 2, z=z - 2, ident=ident)
    if text_x is None:
        text_x = x + 16
    label(text, text_x, y + (h - text_size) // 2 - 2, text_size, fg, z=z)
    hit(cb, x, y, w, h, z=z + 2)


def snackbar(ident, message):
    add("        Snackbar {")
    prop("z_index", 90)
    prop("id", f'"{ident}"')
    prop("message", f'"{message}"')
    prop("duration", 2600)
    prop("show", 0)
    add("        }")


# ---------------------------------------------------------------- palette
BG = "000000"
CARD = "1A1A1C"
CARD_2 = "202023"
PILL = "1A1A1C"
CHIP = "232325"
WELL = "2B2B2E"
TEXT = "FFFFFF"
MUTED = "A6A6AB"
DIM = "77777D"
FAINT = "4C4C4F"
VIOLET = "7B4BEA"
VIOLET_DIM = "4F2E97"
LAVENDER = "B18CF5"
LIME = "C9F25A"
BLUE = "3F9FF6"
AMBER = "F5B94B"
BORDER = "2B2B2E"
HEAT = ["2F224E", "4F2E97", "743BEA", "9060F0", "AC8CF1"]

TEXT_W = {
    12: {"Mar": 21, "Apr": 19, "May": 22, "Jun": 20, "Jul": 16, "Aug": 22,
         "Jan": 19, "Feb": 20, "Sep": 20, "Oct": 23, "Nov": 22, "Dec": 22,
         "Mon": 24, "Tue": 20, "Wed": 25, "Thu": 20, "Fri": 14, "Sat": 17,
         "Sun": 20, "1pm": 24, "2pm": 24, "3pm": 24, "4pm": 24,
         "5pm": 24, "6pm": 24, "Less": 23, "More": 29, "Average": 44,
         "Week": 30, "Month": 36, "Year": 24, "+23%": 30, "+12%": 30,
         "1.24": 23, "Other": 32, "January 26": 60, "February 26": 66,
         "$7,968.00": 60, "$5,957.00": 60, "Peak Thu 4pm": 74},
    14: {"Dashboard": 68, "Analytics": 56, "Transactions": 79, "Reports": 52,
         "Settings": 52, "Transfer": 52, "Request": 52,
         "$4,465.00": 62, "$8,458.70": 62, "-$40.00": 47,
         "-$86.50": 47, "-$248.80": 55, "+$500.00": 59,
         "Available to spend:": 119},
    40: {"$16,957": 145, "$4,182": 129, "$198,420": 194, ".00": 55},
}

# ---------------------------------------------------------------- data seed
income = [46, 50, 43, 36, 33, 36, 43, 51, 63, 79, 93, 97, 89, 71, 53, 39,
          36, 43, 57, 63, 59, 49, 45, 47, 53, 49, 43, 39, 41, 45, 48]
expenses = [22, 19, 21, 26, 30, 27, 19, 17, 21, 28, 32, 37, 39, 35, 31, 27,
            23, 20, 23, 28, 26, 20, 18, 22, 27, 32, 29, 23, 20, 22, 24]

# ============================================================== components
# Reused widgets live in their own files and receive values through the
# inline props of `@embed`, referenced inside as `embed_<name>`.

with Fragment("shell/nav_pill.aroma"):
    card("embed_x", 26, "embed_w", 38, "embed_bg", radius=19, z=2,
         ident="nav_bgembed_idx")
    label("embed_text", "embed_lx", 35, 14, "embed_fg", z=5,
          ident="nav_lbembed_idx")
    hit("embed_cb", "embed_x", 26, "embed_w", 38, z=20)

with Fragment("shell/snackbar.aroma"):
    snackbar("snack_embed_id", "embed_text")

with Fragment("pages/analytics/heat_cell.aroma"):
    card("embed_x", "embed_y", 43, 30, "embed_color", radius=9, z=3,
         ident="heat_embed_idx")
    hit("embed_cb", "embed_x", "embed_y", 43, 30, z=20)

with Fragment("pages/settings/pref_row.aroma"):
    label("embed_name", 672, "embed_y", 14, TEXT, z=4)
    label("embed_desc", 672, "embed_y2", 12, MUTED, z=4)
    switch("switch_prefembed_idx", 1164, "embed_y3", None,
           "dashboard_switch_prefembed_idx", z=6, raw_value="embed_value")

with Fragment("overlays/amount_chip.aroma"):
    pill("embed_text", "embed_x", 382, 70, 34, "232325", "D9D9DE",
         "embed_cb", z=62, text_size=12, text_x="embed_tx")

with Fragment("overlays/search_row.aroma"):
    with Sub("search_resembed_idx"):
        card(372, "embed_y", 536, 54, "232325", radius=14, z=55)
        label("", 390, "embed_y1", 14, TEXT, z=60,
              ident="search_nameembed_idx")
        label("", 390, "embed_y2", 12, MUTED, z=60,
              ident="search_metaembed_idx")
        label("", 790, "embed_y3", 14, TEXT, z=60,
              ident="search_amtembed_idx")
        hit("embed_cb", 372, "embed_y", 536, 54, z=65)

with Fragment("overlays/notif_row.aroma"):
    card(932, "embed_y1", 8, 8, LAVENDER, radius=4, z=60,
         ident="notif_dotembed_idx")
    label("embed_title", 952, "embed_y", 14, TEXT, z=60)
    label("embed_desc", 952, "embed_y2", 12, MUTED, z=60)
    hit("embed_cb", 912, "embed_hy", 306, 54, z=65)

with Fragment("overlays/detail_field.aroma"):
    label("embed_name", 396, "embed_y", 12, MUTED, z=60)
    label("", 396, "embed_y2", 14, TEXT, z=60, ident="embed_id")

# ================================================================== shell
with Fragment("shell/topbar.aroma"):
    with Group("topbar"):
        card(0, 0, 1280, 800, BG, radius=0, z=0)
        image("assets/loud_logo.png", 16, 24, 34, 34, radius=17, z=5)
        label("Loud", 58, 23, 26, TEXT, z=5, ident="loud_brand")
        hit("dashboard_nav_0", 12, 18, 118, 48, z=22)

        nav = [
            ("Dashboard", 376, 108),
            ("Analytics", 494, 90),
            ("Transactions", 594, 120),
            ("Reports", 724, 94),
            ("Settings", 828, 80),
        ]
        for i, (text, x, w) in enumerate(nav):
            active = i == 1
            tw = TEXT_W[14][text]
            embed("nav_pill.aroma", idx=i, x=x, w=w,
                  lx=x + (w - tw) // 2,
                  bg=TEXT if active else PILL,
                  fg="000000" if active else "B9B9BE",
                  text=text, cb=f"dashboard_nav_{i}")

        icon_button("SEARCH", 1104, 26, 40, 18, "1C1C1E", "E4E4E8",
                    cb="dashboard_search_open", z=5, ident="top_search")
        icon_button("MAIL_OUTLINE", 1154, 26, 40, 18, "1C1C1E", "E4E4E8",
                    cb="dashboard_notifications", z=5, ident="top_mail")
        card(1181, 30, 9, 9, "A78BFA", radius=5, z=6, ident="mail_badge")
        image("assets/angela.png", 1208, 22, 44, 44, radius=22, z=5,
              ident="top_avatar")
        hit("dashboard_profile", 1200, 16, 60, 56, z=22)

with Fragment("shell/period.aroma"):
    with Group("period", ident="period_control"):
        card(1024, 126, 232, 40, PILL, radius=20, z=2)
        card(1024, 128, 72, 36, PILL, radius=18, z=3, ident="period_bg_week")
        card(1096, 128, 88, 36, "2E2E30", radius=18, z=3,
             ident="period_bg_month")
        card(1184, 128, 72, 36, PILL, radius=18, z=3, ident="period_bg_year")
        label("Week", 1042, 137, 14, MUTED, z=4, ident="period_lb_week")
        label("Month", 1119, 137, 14, TEXT, z=4, ident="period_lb_month")
        label("Year", 1206, 137, 14, MUTED, z=4, ident="period_lb_year")
        hit("dashboard_period_week", 1024, 126, 72, 40)
        hit("dashboard_period_month", 1096, 126, 88, 40)
        hit("dashboard_period_year", 1184, 126, 72, 40)

with Fragment("shell/snackbars.aroma"):
    with Group("snackbars"):
        snacks = [
            ("transfer", "Transfer scheduled"),
            ("request", "Request created"),
            ("report", "Report downloaded"),
            ("receipt", "Receipt downloaded"),
            ("saved", "Profile saved"),
            ("prefs", "Preferences updated"),
            ("privacy_on", "Balances hidden"),
            ("privacy_off", "Balances visible"),
            ("reviewed", "Marked as reviewed"),
            ("reset", "Demo data restored"),
            ("chart_export", "Chart data downloaded"),
            ("welcome", "Welcome back, Angela"),
            ("heat", "Activity data exported"),
            ("invalid", "Check the highlighted fields"),
        ]
        for ident, message in snacks:
            embed("snackbar.aroma", id=ident, text=message)

# ======================================================== analytics page
with Fragment("pages/analytics/heading.aroma"):
    begin_section()
    label("Welcome back, Angela", 24, 120, 40, TEXT, z=4,
          ident="welcome_heading")

    end_section()

with Fragment("pages/analytics/revenue.aroma"):
    begin_section()
    # ------------------------------------------------ revenue + actions
    label("Total revenue", 24, 200, 16, MUTED, z=4)
    label("$16,957", 24, 240, 40, TEXT, z=4, ident="revenue_main")
    label(".00", 171, 240, 40, DIM, z=4, ident="revenue_cents")
    label("+12,67%", 238, 264, 12, BLUE, z=4, ident="revenue_delta")
    label("Available to spend:", 24, 314, 14, MUTED, z=4)
    label("$16,957.00", 148, 314, 14, TEXT, z=4, ident="available_value")

    card(24, 354, 106, 40, TEXT, radius=20, z=2)
    label("Transfer", 40, 366, 14, "000000", z=4)
    icon("ARROW_UPWARD", 94, 368, 14, "000000", z=5)
    hit("dashboard_transfer", 24, 354, 106, 40)

    card(140, 354, 106, 40, TEXT, radius=20, z=2)
    label("Request", 156, 366, 14, "000000", z=4)
    icon("ARROW_DOWNWARD", 210, 368, 14, "000000", z=5)
    hit("dashboard_request", 140, 354, 106, 40)

    card(258, 354, 40, 40, BG, radius=20, z=2, kind="outlined",
         border="343437")
    icon("MORE_VERT", 269, 365, 16, "D9D9DE", z=5)
    hit("dashboard_quick_menu", 258, 354, 40, 40)

    end_section()

with Fragment("pages/analytics/bars.aroma"):
    begin_section()
    # ------------------------------------------------------ revenue bars
    card(429, 195, 1, 214, BORDER, radius=0, z=2)
    card(768, 195, 1, 214, BORDER, radius=0, z=2)
    card(1121, 195, 1, 214, BORDER, radius=0, z=2)

    label("$4,465.00", 448, 208, 16, TEXT, z=4, ident="stat_invest_value")
    label("$8,458.70", 784, 182, 16, TEXT, z=4, ident="stat_products_value")

    label("+23%", 448, 314, 12, TEXT, z=4, ident="stat_invest_delta")
    label("Invest", 486, 314, 12, MUTED, z=4)
    label("+12%", 784, 314, 12, TEXT, z=4, ident="stat_products_delta")
    label("Products", 822, 314, 12, MUTED, z=4)
    label("1.24", 1136, 314, 12, TEXT, z=4, ident="stat_other_value")
    label("Other", 1173, 314, 12, MUTED, z=4)

    card(448, 346, 290, 44, "242426", radius=10, z=3)
    card(448, 346, 290, 44, VIOLET, radius=10, z=3, ident="invest_fill")

    product_heights = [31, 28, 35, 32, 26, 24, 31, 34, 28, 37, 33, 27, 29,
                       31, 26, 28, 34, 30, 27, 32]
    shades = ["6E41E6", "7446E9", "7B4BEA", "8252EC", "8A5AED",
              "9365EF", "9C70F0", "A67CF2", "AF87F4", "B18CF5"]
    for i, h in enumerate(product_heights):
        x = 784 + round(i * 16.5)
        card(x, 390 - h, 7, h, shades[i % len(shades)], radius=4, z=3,
             ident=f"product_bar_{i}")

    other_heights = [29, 25, 32, 27, 24, 30, 26, 23]
    for i, h in enumerate(other_heights):
        card(1136 + i * 13, 390 - h, 7, h, "4E3195", radius=4, z=3,
             ident=f"other_bar_{i}")

    label("January 26", 448, 402, 12, MUTED, z=4, ident="bars_date_start")
    label("February 26", 1184, 402, 12, MUTED, z=4, ident="bars_date_end")

    end_section()

with Fragment("pages/analytics/average.aroma"):
    begin_section()
    # -------------------------------------------- dashed average guide
    x = 433
    while x < 1253:
        card(x, 268, 6, 1, FAINT, radius=0, z=3)
        x += 16
    card(988, 243, 100, 26, CHIP, radius=13, z=4)
    label("Average", 1010, 249, 12, "E8E8EA", z=5)

    end_section()

with Fragment("pages/analytics/chart_card.aroma"):
    begin_section()
    # ------------------------------------------------------ analytics card
    card(24, 440, 402, 338, CARD, radius=22, z=2, interactive=True)
    icon("MULTILINE_CHART", 48, 462, 20, TEXT, z=4)
    label("Analytics", 78, 460, 20, TEXT, z=4)
    icon_button("MORE_VERT", 370, 458, 40, 18, CHIP, "BCBCC1",
                cb="dashboard_chart_menu", z=5)

    card(50, 482, 14, 14, LIME, radius=4, z=3)
    label("Income", 72, 478, 14, TEXT, z=4)
    card(132, 482, 14, 14, "74747A", radius=4, z=3)
    label("Expenses", 154, 478, 14, MUTED, z=4)

    chart("expense_chart", 50, 530, 352, 204, expenses, "6D6878", 2.2, 4)
    chart("income_chart", 50, 530, 352, 204, income, LIME, 2.5, 5)

    month_names = ["Mar", "Apr", "May", "Jun", "Jul", "Aug",
                   "Sep", "Oct", "Nov", "Dec", "Jan", "Feb"]
    month_widths = [21, 19, 22, 20, 16, 22, 20, 23, 22, 22, 19, 20]
    for i, (name, tw) in enumerate(zip(month_names, month_widths)):
        cx = 50 + i * 70.4
        label(name, round(cx - tw / 2), 742, 12, MUTED, z=4,
              ident=f"chart_x{i}", visible=i < 6)

    with Sub("chart_marker"):
        yy = 536
        while yy < 726:
            card(261, yy, 1, 8, "3E3E42", radius=0, z=4)
            yy += 16
        card(288, 510, 96, 26, CHIP, radius=13, z=6)
        card(296, 518, 3, 10, LIME, radius=2, z=7)
        label("$7,968.00", 306, 516, 12, TEXT, z=7,
              ident="chart_callout_income")
        card(288, 542, 96, 26, CHIP, radius=13, z=6)
        card(296, 550, 3, 10, "74747A", radius=2, z=7)
        label("$5,957.00", 306, 548, 12, TEXT, z=7,
              ident="chart_callout_expense")

    end_section()

with Fragment("pages/analytics/activity_card.aroma"):
    begin_section()
    # ------------------------------------------------------- activity card
    card(438, 440, 404, 338, CARD, radius=22, z=2, interactive=True)
    icon("PUBLIC", 462, 462, 20, TEXT, z=4)
    label("Activity by time", 486, 460, 20, TEXT, z=4)
    label("Peak Thu 4pm", 650, 466, 12, LIME, z=4, ident="heat_info")
    icon_button("OPEN_IN_NEW", 782, 458, 40, 18, CHIP, "BCBCC1",
                cb="dashboard_export_heatmap", z=5)

    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    day_x = [504 + i * 47 + round((43 - TEXT_W[12][d]) / 2)
             for i, d in enumerate(days)]
    for d, x in zip(days, day_x):
        label(d, x, 508, 12, MUTED, z=4)

    levels = [
        [0, 0, 0, 1, 0, 0, 0],
        [0, 0, 2, 3, 3, 0, 0],
        [0, 3, 2, 2, 2, 3, 0],
        [1, 1, 2, 4, 2, 2, 1],
        [0, 1, 1, 2, 4, 0, 0],
        [0, 0, 0, 1, 0, 0, 0],
    ]
    times = ["1pm", "2pm", "3pm", "4pm", "5pm", "6pm"]
    cell = 0
    for row, (lv, tm) in enumerate(zip(levels, times)):
        y = 540 + row * 34
        label(tm, 464, y + 8, 12, MUTED, z=4)
        for col, level in enumerate(lv):
            embed("heat_cell.aroma", idx=cell, x=504 + col * 47, y=y,
                  color=HEAT[level], cb=f"dashboard_heat_{cell}")
            cell += 1

    label("Less", 664, 748, 12, MUTED, z=4)
    for i, color in enumerate(HEAT):
        card(694 + i * 17, 750, 10, 10, color, radius=3, z=4)
    label("More", 790, 748, 12, MUTED, z=4)

    end_section()

with Fragment("pages/analytics/transactions_card.aroma"):
    begin_section()
    # --------------------------------------------------- recent transactions
    card(854, 440, 402, 338, CARD, radius=22, z=2, interactive=True)
    icon("HISTORY", 876, 462, 20, TEXT, z=4)
    label("Recent transactions", 902, 460, 20, TEXT, z=4)
    icon_button("SEARCH", 1210, 458, 40, 18, CHIP, "BCBCC1",
                cb="dashboard_search_open", z=5)

    rows = [
        ("Internet", "Multimedia", LIME, "-$40.00", 526),
        ("Isabella Garcia", "Transfer", "8B5CF6", "-$86.50", 574),
        ("Sephora", "Beauty", "31B6C8", "-$248.80", 622),
        ("Netflix", "Multimedia", LIME, "-$248.80", 670),
        ("Violet Oream", "Transfer", "8B5CF6", "+$500.00", 718),
    ]
    for i, (name, category, dot_color, amount, y) in enumerate(rows):
        label(name, 878, y, 14, TEXT, z=4, ident=f"txn_name{i}")
        card(1002, y - 2, 88, 28, "242426", radius=14, z=3,
             ident=f"txn_pill{i}")
        card(1014, y + 8, 8, 8, dot_color, radius=4, z=4,
             ident=f"txn_dot{i}")
        label(category, 1028, y + 4, 12, "C6C6CB", z=4,
              ident=f"txn_cat{i}")
        aw = TEXT_W[14].get(amount, 47)
        label(amount, 1222 - aw, y + 1, 14, TEXT, z=4,
              ident=f"txn_amt{i}")
        icon("MORE_VERT", 1234, y + 4, 16, "9A9A9F", z=4)
        hit(f"dashboard_txn_{i}", 854, y - 10, 402, 46, z=20)

    end_section()

with Fragment("pages/analytics.aroma"):
    with Group("analytics", ident="page_analytics"):
        embed("analytics/heading.aroma")
        embed("analytics/revenue.aroma")
        embed("analytics/bars.aroma")
        embed("analytics/average.aroma")
        embed("analytics/chart_card.aroma")
        embed("analytics/activity_card.aroma")
        embed("analytics/transactions_card.aroma")

# ========================================================== dashboard page
with Fragment("pages/dashboard/kpis.aroma"):
    kpis = [
        ("ACCOUNT_BALANCE_WALLET", "Available balance", "$16,957.00",
         "Across 2 accounts", "dash_available", VIOLET, 24),
        ("TRENDING_UP", "Revenue selected period", "$16,957.00",
         "+12.67% vs last period", "dash_revenue", LIME, 440),
        ("SCHEDULE", "Pending inflow", "$1,850.00",
         "1 request awaiting payment", "dash_pending", AMBER, 856),
    ]
    for i, (icon_name, title, value, sub, ident, accent, x) in enumerate(kpis):
        card(x, 200, 400, 124, CARD, radius=20, z=2, interactive=True)
        icon(icon_name, x + 24, 222, 20, accent, z=4)
        label(title, x + 52, 220, 14, MUTED, z=4)
        label(value, x + 24, 250, 26, TEXT, z=4, ident=ident)
        label(sub, x + 24, 288, 12, MUTED, z=4, ident=f"dash_sub{i}")

with Fragment("pages/dashboard/cashflow.aroma"):
    # -------------------------------------------------------- cash flow card
    card(24, 344, 804, 436, CARD, radius=22, z=2, interactive=True)
    icon("MULTILINE_CHART", 48, 366, 20, TEXT, z=4)
    label("Cash flow", 78, 364, 20, TEXT, z=4)
    label("Income and expenses for the selected period", 78, 394, 12, MUTED,
          z=4)
    icon_button("OPEN_IN_NEW", 764, 362, 40, 18, CHIP, "BCBCC1",
                cb="dashboard_goto_analytics", z=5)

    card(52, 430, 14, 14, LIME, radius=4, z=3)
    label("Income", 72, 427, 14, TEXT, z=4)
    card(132, 430, 14, 14, "74747A", radius=4, z=3)
    label("Expenses", 154, 427, 14, MUTED, z=4)

    chart("dash_expense_chart", 52, 466, 752, 286, expenses, "6D6878", 2.2, 4)
    chart("dash_income_chart", 52, 466, 752, 286, income, LIME, 2.5, 5)
    dash_names = ["Mar", "Apr", "May", "Jun", "Jul", "Aug",
                  "Sep", "Oct", "Nov", "Dec", "Jan", "Feb"]
    for i, name in enumerate(dash_names):
        x = 52 + round(i * 752 / 5)
        label(name, x - 10, 754, 12, MUTED, z=4, ident=f"dash_x{i}",
              visible=i < 6)

with Fragment("pages/dashboard/activity_card.aroma"):
    # ----------------------------------------------------- recent activity
    card(844, 344, 412, 236, CARD, radius=22, z=2, interactive=True)
    icon("HISTORY", 868, 366, 20, TEXT, z=4)
    label("Recent activity", 894, 364, 20, TEXT, z=4)
    for i in range(3):
        y = 414 + i * 46
        card(868, y, 364, 38, CARD_2, radius=12, z=3)
        label("", 882, y + 11, 14, TEXT, z=4, ident=f"dash_txn_name{i}")
        label("", 1176, y + 11, 12, MUTED, z=4, ident=f"dash_txn_amt{i}")
        hit(f"dashboard_dash_txn_{i}", 868, y, 364, 38, z=20)

with Fragment("pages/dashboard/actions.aroma"):
    # -------------------------------------------------------- quick actions
    card(844, 596, 412, 184, CARD, radius=22, z=2, interactive=True)
    label("Quick actions", 868, 614, 20, TEXT, z=4)
    pill("New transfer", 868, 654, 180, 44, TEXT, "000000",
         "dashboard_transfer", text_size=14, text_x=898)
    pill("Request money", 1060, 654, 172, 44, "2A2438", "E9E1FF",
         "dashboard_request", text_size=14, text_x=1086)
    pill("Export statement", 868, 712, 364, 44, "232325", "FFFFFF",
         "dashboard_export_statement", text_size=14, text_x=1014)

with Fragment("pages/dashboard.aroma"):
    with Group("dashboard", ident="page_dashboard", visible=False):
        label("Overview", 24, 120, 40, TEXT, z=4)
        label("Here is how your money is moving this month.", 24, 172, 14,
              MUTED, z=4)
        embed("dashboard/kpis.aroma")
        embed("dashboard/cashflow.aroma")
        embed("dashboard/activity_card.aroma")
        embed("dashboard/actions.aroma")

# ======================================================= transactions page
with Fragment("pages/transactions.aroma"):
    with Group("transactions", ident="page_transactions", visible=False):
        label("Transactions", 24, 120, 40, TEXT, z=4)
        label("Search, review, and export every movement in your account.", 24,
              172, 14, MUTED, z=4)

        pill("Export CSV", 1120, 146, 136, 42, TEXT, "000000",
             "dashboard_export_transactions", text_size=14, text_x=1150)

        textbox("txn_search_input", 24, 214, 480, 44,
                "Search by name or category", "dashboard_txn_search", z=6)

        filters = [("All", 520, 74), ("Income", 600, 92),
                   ("Expenses", 698, 104), ("Transfers", 808, 112)]
        for i, (text, x, w) in enumerate(filters):
            bg = "FFFFFF" if i == 0 else "1A1A1C"
            fg = "000000" if i == 0 else "B9B9BE"
            card(x, 216, w, 40, bg, radius=20, z=2,
                 ident=f"txn_filter_bg{i}")
            label(text, x + 18, 227, 14, fg, z=4,
                  ident=f"txn_filter_lb{i}")
            hit(f"dashboard_txn_filter_{i}", x, 216, w, 40, z=20)

        card(24, 284, 1232, 496, CARD, radius=22, z=2, interactive=True)
        label("Transaction", 48, 304, 12, MUTED, z=4)
        label("Category", 504, 304, 12, MUTED, z=4)
        label("Date", 700, 304, 12, MUTED, z=4)
        label("Status", 888, 304, 12, MUTED, z=4)
        label("Amount", 1184, 304, 12, MUTED, z=4)
        card(48, 332, 1184, 1, BORDER, radius=0, z=3)

        for i in range(8):
            y = 348 + i * 50
            label("", 48, y + 9, 14, TEXT, z=4, ident=f"tab_name{i}")
            card(504, y + 6, 112, 28, "242426", radius=14, z=3,
                 ident=f"tab_pill{i}")
            card(516, y + 16, 8, 8, LIME, radius=4, z=4, ident=f"tab_dot{i}")
            label("", 530, y + 12, 12, "C6C6CB", z=4, ident=f"tab_cat{i}")
            label("", 700, y + 9, 12, MUTED, z=4, ident=f"tab_date{i}")
            card(888, y + 6, 100, 28, "242426", radius=14, z=3,
                 ident=f"tab_status_pill{i}")
            label("", 900, y + 12, 12, LIME, z=4, ident=f"tab_status{i}")
            label("", 1176, y + 8, 14, TEXT, z=4, ident=f"tab_amt{i}")
            hit(f"dashboard_txnrow_{i}", 24, y, 1232, 44, z=20,
                ident=f"tab_hit{i}")

        label("No transactions match this filter.", 48, 380, 16, MUTED,
              z=4, ident="txn_empty", visible=False)
        label("Showing 0 of 0 transactions", 48, 752, 12, MUTED, z=4,
              ident="txn_count")

# ============================================================ reports page
with Fragment("pages/reports.aroma"):
    with Group("reports", ident="page_reports", visible=False):
        label("Reports", 24, 120, 40, TEXT, z=4)
        label("Generate a real CSV from the data in this demo.", 24, 172, 14,
              MUTED, z=4)
        label("Reporting period", 1024, 174, 12, MUTED, z=4)
        label("August 2026", 1024, 192, 14, TEXT, z=4, ident="report_period")

        reports = [
            ("RECEIPT", "Monthly statement",
             "Every transaction in the selected period, ready for your "
             "accountant.",
             "0"),
            ("TRENDING_UP", "Income by category",
             "Grouped income totals with category share for the selected "
             "period.",
             "1"),
            ("TRENDING_DOWN", "Expense report",
             "Categorized spending with pending items clearly flagged.",
             "2"),
            ("PUBLIC", "Activity export",
             "Raw activity heatmap data for the current time window.",
             "3"),
        ]
        positions = [(24, 230), (664, 230), (24, 486), (664, 486)]
        for (icon_name, title, desc, which), (x, y) in zip(reports, positions):
            card(x, y, 592, 232, CARD, radius=22, z=2, interactive=True)
            card(x + 24, y + 24, 48, 48, "2A2438", radius=14, z=3)
            icon(icon_name, x + 38, y + 38, 20, LAVENDER, z=4)
            label(title, x + 88, y + 26, 20, TEXT, z=4)
            label(desc, x + 24, y + 92, 14, MUTED, z=4)
            label("CSV  ·  generated from local demo data", x + 24, y + 158,
                  12, DIM, z=4)
            pill("Download CSV", x + 24, y + 180, 156, 34, "232325", "FFFFFF",
                 f"dashboard_report_{which}", text_size=12, text_x=x + 47)

# =========================================================== settings page
with Fragment("pages/settings.aroma"):
    with Group("settings", ident="page_settings", visible=False):
        label("Settings", 24, 120, 40, TEXT, z=4)
        label("Manage your profile, alerts, privacy, and demo session.", 24,
              172, 14, MUTED, z=4)

        # ---------------------------------------------------------- profile
        card(24, 206, 600, 252, CARD, radius=22, z=2, interactive=True)
        icon("PERSON", 48, 228, 20, TEXT, z=4)
        label("Profile", 78, 226, 20, TEXT, z=4)
        label("Display name", 48, 270, 12, MUTED, z=4)
        textbox("settings_name", 48, 290, 552, 42, "Your name", z=6)
        label("Email", 48, 346, 12, MUTED, z=4)
        textbox("settings_email", 48, 366, 360, 42, "you@example.com", z=6)
        pill("Save profile", 420, 366, 180, 42, TEXT, "000000",
             "dashboard_settings_save", text_size=14, text_x=460)
        label("", 48, 424, 12, MUTED, z=4, ident="settings_status")

        # ---------------------------------------------------- notifications
        card(648, 206, 608, 252, CARD, radius=22, z=2, interactive=True)
        icon("NOTIFICATIONS", 672, 228, 20, TEXT, z=4)
        label("Notifications", 702, 226, 20, TEXT, z=4)
        prefs = [("Weekly summary",
                  "Spending and revenue digest every Monday.", 0, True, 278),
                 ("Large transactions",
                  "Alert me when a movement exceeds $250.", 1, True, 334),
                 ("Product updates",
                  "Occasional news about new features.", 2, False, 390)]
        for name, desc, idx, val, y in prefs:
            embed("settings/pref_row.aroma", name=name, desc=desc, idx=idx,
                  value="true" if val else "false", y=y, y2=y + 20,
                  y3=y - 2)
        label("3 of 3 alerts enabled", 672, 430, 12, LIME, z=4,
              ident="settings_pref_status")

        # ------------------------------------------------ privacy & session
        card(24, 486, 600, 290, CARD, radius=22, z=2, interactive=True)
        icon("SECURITY", 48, 508, 20, TEXT, z=4)
        label("Privacy & security", 78, 506, 20, TEXT, z=4)
        label("Hide balances", 48, 552, 14, TEXT, z=4)
        label("Mask every amount until you turn this off again.", 48, 574,
              12, MUTED, z=4)
        switch("switch_privacy", 540, 554, False, "dashboard_switch_privacy",
               z=6)
        label("Balances are visible", 48, 610, 12, LIME, z=4,
              ident="settings_privacy_status")
        card(48, 650, 524, 1, BORDER, radius=0, z=3)
        pill("Reset demo data", 48, 678, 208, 44, "232325", "FFFFFF",
             "dashboard_reset_confirm", text_size=14, text_x=78)
        pill("Sign out", 274, 678, 146, 44, "232325", "FFFFFF",
             "dashboard_signout_confirm", text_size=14, text_x=314)

        # -------------------------------------------------------- workspace
        card(648, 486, 608, 290, CARD, radius=22, z=2, interactive=True)
        icon("INFO", 672, 508, 20, TEXT, z=4)
        label("Workspace", 702, 506, 20, TEXT, z=4)
        rows = [("Workspace", "Loud Analytics"), ("Plan", "Pro - annual"),
                ("Signed in as", "angela@loud.app"),
                ("Data residency", "Local demo data")]
        for i, (key, value) in enumerate(rows):
            y = 558 + i * 40
            label(key, 672, y, 12, MUTED, z=4)
            label(value, 888, y, 14, TEXT, z=4,
                  ident="settings_account" if i == 2 else None)
        pill("Download account data", 672, 708, 240, 44, "232325", "FFFFFF",
             "dashboard_export_account", text_size=14, text_x=700)

# =============================================================== overlays
# Every overlay is a hidden container with a dimmed backdrop and one panel.
# The backdrop button closes it; the panel blocker swallows interior clicks.

with Fragment("overlays/search.aroma"):
    with Group("ov_search", ident="overlay_search", visible=False):
        card(0, 0, 1280, 800, "B3000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(340, 80, 600, 620, CARD, radius=24, z=52)
        hit("dashboard_noop", 340, 80, 600, 620, z=53)
        label("Search", 372, 106, 26, TEXT, z=60)
        icon_button("CLOSE", 860, 100, 44, 18, "232325", "BCBCC1",
                    cb="dashboard_close_overlay", z=70)
        textbox("search_input", 372, 154, 536, 46,
                "Search transactions, categories, or amounts",
                "dashboard_search_change", z=60)
        label("RECENT AND MATCHING TRANSACTIONS", 372, 220, 12, DIM, z=60)
        for i in range(5):
            y = 250 + i * 64
            embed("search_row.aroma", idx=i, y=y, y1=y + 10, y2=y + 31,
                  y3=y + 17, cb=f"dashboard_search_hit{i}")
        label("No transactions match your search.", 372, 276, 16, MUTED,
              z=60, ident="search_empty", visible=False)
        label("Click outside to close", 372, 662, 12, DIM, z=60)

with Fragment("overlays/notifications.aroma"):
    with Group("ov_notif", ident="overlay_notifications", visible=False):
        card(0, 0, 1280, 800, "80000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(888, 78, 354, 342, CARD, radius=22, z=52)
        hit("dashboard_noop", 888, 78, 354, 342, z=53)
        label("Notifications", 914, 96, 20, TEXT, z=60)
        label("Mark all read", 1104, 102, 12, LAVENDER, z=60)
        hit("dashboard_notifications_read_all", 1084, 90, 134, 30, z=65)
        card(914, 132, 302, 1, BORDER, radius=0, z=55)
        notes = [("Transfer completed", "Your $500 transfer cleared.", 0),
                 ("Report ready", "The August statement is ready.", 1),
                 ("Weekly summary", "Spending is down 8% this week.", 2),
                 ("New sign-in", "Chrome on Linux - just now.", 3)]
        for i, (title, desc, nav) in enumerate(notes):
            y = 150 + i * 62
            embed("notif_row.aroma", idx=i, y=y + 7, y1=y + 14, y2=y + 28,
                  hy=y, title=title, desc=desc, cb=f"dashboard_notif_{i}")

with Fragment("overlays/profile.aroma"):
    with Group("ov_profile", ident="overlay_profile", visible=False):
        card(0, 0, 1280, 800, "80000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(1012, 78, 230, 244, CARD, radius=22, z=52)
        hit("dashboard_noop", 1012, 78, 230, 244, z=53)
        label("Angela Martin", 1036, 96, 16, TEXT, z=60,
              ident="profile_name")
        label("angela@loud.app", 1036, 120, 12, MUTED, z=60,
              ident="profile_email")
        card(1036, 148, 182, 1, BORDER, radius=0, z=55)
        items = [("Account settings", 160, "dashboard_profile_settings",
                  TEXT),
                 ("Billing & plan", 198, "dashboard_profile_billing", TEXT),
                 ("Sign out", 236, "dashboard_profile_signout", "FF9CAE")]
        for text, y, cb, color in items:
            label(text, 1036, y, 14, color, z=60)
            hit(cb, 1024, y - 8, 206, 34, z=65)

with Fragment("overlays/transfer.aroma"):
    with Group("ov_transfer", ident="overlay_transfer", visible=False):
        card(0, 0, 1280, 800, "B3000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(340, 110, 600, 580, CARD, radius=24, z=52)
        hit("dashboard_noop", 340, 110, 600, 580, z=53)
        label("Transfer money", 372, 136, 26, TEXT, z=60)
        label("Send from Checking  •••• 4821", 372, 174, 12, MUTED, z=60)
        label("Available", 700, 174, 12, MUTED, z=60)
        label("$16,957.00", 770, 172, 14, TEXT, z=60,
              ident="transfer_available")
        icon_button("CLOSE", 860, 130, 44, 18, "232325", "BCBCC1",
                    cb="dashboard_close_overlay", z=70)

        label("Recipient", 372, 216, 12, MUTED, z=60)
        textbox("transfer_recipient", 372, 236, 536, 46,
                "Name or @handle", z=60)
        label("Amount", 372, 300, 12, MUTED, z=60)
        textbox("transfer_amount", 372, 320, 536, 46, "0.00", z=60)
        for i, (text, x) in enumerate([("$50", 372), ("$100", 452),
                                       ("$500", 532)]):
            embed("amount_chip.aroma", text=text, x=x, tx=x + 23,
                  cb=f"dashboard_transfer_amount_{i}")
        label("", 372, 432, 12, "FF9CAE", z=60, ident="transfer_error",
              visible=False)
        pill("Cancel", 372, 598, 150, 44, "232325", "FFFFFF",
             "dashboard_close_overlay", z=62, text_size=14, text_x=420)
        pill("Send transfer", 774, 598, 134, 44, TEXT, "000000",
             "dashboard_transfer_submit", z=62, text_size=14, text_x=797)

with Fragment("overlays/request.aroma"):
    with Group("ov_request", ident="overlay_request", visible=False):
        card(0, 0, 1280, 800, "B3000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(340, 110, 600, 580, CARD, radius=24, z=52)
        hit("dashboard_noop", 340, 110, 600, 580, z=53)
        label("Request money", 372, 136, 26, TEXT, z=60)
        label("Create a payment link for a client or friend.", 372, 174, 12,
              MUTED, z=60)
        icon_button("CLOSE", 860, 130, 44, 18, "232325", "BCBCC1",
                    cb="dashboard_close_overlay", z=70)
        label("Who should pay?", 372, 216, 12, MUTED, z=60)
        textbox("request_payer", 372, 236, 536, 46,
                "Name or email", z=60)
        label("Amount", 372, 300, 12, MUTED, z=60)
        textbox("request_amount", 372, 320, 536, 46, "0.00", z=60)
        for i, (text, x) in enumerate([("$50", 372), ("$100", 452),
                                       ("$500", 532)]):
            embed("amount_chip.aroma", text=text, x=x, tx=x + 23,
                  cb=f"dashboard_request_amount_{i}")
        label("", 372, 432, 12, "FF9CAE", z=60, ident="request_error",
              visible=False)
        pill("Cancel", 372, 598, 150, 44, "232325", "FFFFFF",
             "dashboard_close_overlay", z=62, text_size=14, text_x=420)
        pill("Create request", 774, 598, 134, 44, TEXT, "000000",
             "dashboard_request_submit", z=62, text_size=14, text_x=795)

with Fragment("overlays/txn_detail.aroma"):
    with Group("ov_detail", ident="overlay_txn_detail", visible=False):
        card(0, 0, 1280, 800, "B3000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(364, 90, 552, 610, CARD, radius=24, z=52)
        hit("dashboard_noop", 364, 90, 552, 610, z=53)
        label("Transaction details", 396, 116, 24, TEXT, z=60)
        icon_button("CLOSE", 844, 110, 44, 18, "232325", "BCBCC1",
                    cb="dashboard_close_overlay", z=70)
        card(396, 166, 488, 126, "232325", radius=18, z=55)
        label("", 420, 188, 18, MUTED, z=60, ident="detail_name")
        label("", 420, 218, 40, TEXT, z=60, ident="detail_amount")
        label("", 420, 266, 12, MUTED, z=60, ident="detail_date")
        fields = [("Category", "detail_category", 322),
                  ("Status", "detail_status", 372),
                  ("Transaction ID", "detail_id", 422),
                  ("Payment method", "detail_method", 472)]
        for name, ident, y in fields:
            embed("detail_field.aroma", name=name, id=ident, y=y, y2=y + 18)
        card(396, 520, 488, 1, BORDER, radius=0, z=55)
        pill("Download receipt", 396, 570, 190, 44, "232325", "FFFFFF",
             "dashboard_detail_receipt", z=62, text_size=14, text_x=424)
        pill("Mark reviewed", 600, 570, 172, 44, TEXT, "000000",
             "dashboard_detail_review", z=62, text_size=14, text_x=630)

with Fragment("overlays/quick.aroma"):
    with Group("ov_quick", ident="overlay_quick", visible=False):
        card(0, 0, 1280, 800, "66000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(210, 402, 236, 168, CARD, radius=18, z=52)
        hit("dashboard_noop", 210, 402, 236, 168, z=53)
        items = [("View all transactions", 416, "dashboard_goto_transactions"),
                 ("Export account data", 464, "dashboard_export_account"),
                 ("Reset demo data", 512, "dashboard_reset_confirm")]
        for text, y, cb in items:
            label(text, 230, y, 14, TEXT, z=60)
            hit(cb, 218, y - 8, 220, 34, z=65)

with Fragment("overlays/chart_menu.aroma"):
    with Group("ov_chart", ident="overlay_chart_menu", visible=False):
        card(0, 0, 1280, 800, "66000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(168, 508, 242, 168, CARD, radius=18, z=52)
        hit("dashboard_noop", 168, 508, 242, 168, z=53)
        items = [("Export chart data", 522, "dashboard_export_chart"),
                 ("View reports", 570, "dashboard_goto_reports"),
                 ("Reset to month view", 618, "dashboard_reset_period")]
        for text, y, cb in items:
            label(text, 188, y, 14, TEXT, z=60)
            hit(cb, 176, y - 8, 226, 34, z=65)

with Fragment("overlays/confirm.aroma"):
    with Group("ov_confirm", ident="overlay_confirm", visible=False):
        card(0, 0, 1280, 800, "CC000000", radius=0, z=50)
        hit("dashboard_close_overlay", 0, 0, 1280, 800, z=51)
        card(410, 236, 460, 268, CARD, radius=24, z=52)
        hit("dashboard_noop", 410, 236, 460, 268, z=53)
        label("", 442, 268, 24, TEXT, z=60, ident="confirm_title")
        label("", 442, 312, 14, MUTED, z=60, ident="confirm_message")
        pill("Cancel", 442, 424, 140, 44, "232325", "FFFFFF",
             "dashboard_close_overlay", z=62, text_size=14, text_x=483)
        pill("Confirm", 730, 424, 108, 44, "FF375F", "FFFFFF",
             "dashboard_confirm_yes", z=62, text_size=14, text_x=752)

with Fragment("overlays/signedout.aroma"):
    with Group("ov_signedout", ident="overlay_signedout", visible=False):
        card(0, 0, 1280, 800, BG, radius=0, z=50)
        image("assets/loud_logo.png", 608, 218, 64, 64, radius=32, z=55,
              ident="signedout_logo")
        label("You've been signed out", 458, 338, 32, TEXT, z=55,
              ident="signedout_title")
        label("This demo does not store real account data.", 500, 390, 14,
              MUTED, z=55, ident="signedout_note")
        pill("Sign back in", 544, 444, 192, 48, TEXT, "000000",
             "dashboard_sign_back_in", z=62, text_size=14, text_x=596)

# ================================================================ root
ROOT_TOKENS.extend([
    "Window {",
    "    width: 1280",
    "    height: 800",
    '    title: "Loud"',
])
for path in [
    "shell/topbar.aroma",
    "shell/period.aroma",
    "pages/analytics.aroma",
    "pages/dashboard.aroma",
    "pages/transactions.aroma",
    "pages/reports.aroma",
    "pages/settings.aroma",
    "overlays/search.aroma",
    "overlays/notifications.aroma",
    "overlays/profile.aroma",
    "overlays/transfer.aroma",
    "overlays/request.aroma",
    "overlays/txn_detail.aroma",
    "overlays/quick.aroma",
    "overlays/chart_menu.aroma",
    "overlays/confirm.aroma",
    "overlays/signedout.aroma",
    "shell/snackbars.aroma",
]:
    ROOT_TOKENS.append(Embed(path, None, "    "))
ROOT_TOKENS.append("}")

# ------------------------------------------------------- flattening / split


def refs_of(text):
    return set(EMBED_REF_RE.findall(text))


def substitute(text, props):
    def repl(match):
        return props.get(match.group(1), match.group(0))
    return EMBED_REF_RE.sub(repl, text)


def child_path(base, path):
    """Resolve an embed path the way the C resolver does: relative to the
    embedding file's directory (``base`` is that directory)."""
    if not base:
        return path
    return os.path.normpath(os.path.join(base, path))


def render(tokens, props, where="root", base=""):
    """Inline every embed, mirroring the C resolver's prop substitution."""
    result = []
    for tok in tokens:
        if isinstance(tok, Embed):
            rel = child_path(base, tok.path)
            frag = FRAGMENTS.get(rel)
            if frag is None:
                raise GenError(f"{where}: no fragment for '{tok.path}'")
            resolved = {}
            for key, value in tok.props.items():
                missing = refs_of(value) - set(props)
                if missing:
                    raise GenError(
                        f"{where}: prop '{key}' of '{tok.path}' references "
                        f"{sorted(missing)} which is not passed here")
                resolved[key] = substitute(value, props)
            merged = dict(props)
            merged.update(resolved)
            result.extend(render(frag.lines, merged, rel, os.path.dirname(rel) if "/" in rel else ""))
        else:
            missing = refs_of(tok) - set(props)
            if missing:
                raise GenError(
                    f"{where}: {sorted(missing)} referenced but not passed "
                    f"by @embed")
            result.append(substitute(tok, props))
    return result


def serialize(tokens):
    lines = []
    for tok in tokens:
        if isinstance(tok, Embed):
            props = ", ".join(f'{k}: "{v}"' for k, v in tok.props.items())
            line = f'{tok.indent}@embed "{tok.path}"'
            if props:
                line += f" {{ {props} }}"
            lines.append(line)
        else:
            lines.append(tok)
    return "\n".join(lines) + "\n"


def flat_markup():
    return "\n".join(render(ROOT_TOKENS, {})) + "\n"


def write_split(target):
    flat_markup()  # validate props before writing anything
    written = {"app.aroma": serialize(ROOT_TOKENS)}
    for path, frag in FRAGMENTS.items():
        written[path] = serialize(frag.lines)

    os.makedirs(target, exist_ok=True)
    for rel, text in sorted(written.items()):
        dest = os.path.join(target, rel)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        header = (f"// {rel} - generated by "
                  f"tools/sandbox/gen_dashboard.py --split\n")
        with open(dest, "w", encoding="utf-8") as fh:
            fh.write(header + text)

    keep = set(written)
    removed = []
    for root, _dirs, files in os.walk(target):
        for name in files:
            if not name.endswith(".aroma"):
                continue
            rel = os.path.relpath(os.path.join(root, name), target)
            if rel not in keep:
                os.remove(os.path.join(root, name))
                removed.append(rel)
    print(f"wrote {len(written)} files to {target}")
    for rel in removed:
        print(f"removed stale {rel}")


def update_docs_sandbox():
    """Splice the flat markup into docs/sandbox.html's demo table.

    The table stores each example as a single-quoted JavaScript string with
    escaped newlines, so re-escape the markup and swap the content between
    the 'dashboard_tablet': '...' delimiters.
    """
    repo_root = os.path.dirname(os.path.dirname(
        os.path.dirname(os.path.abspath(__file__))))
    html_path = os.path.join(repo_root, "docs", "sandbox.html")

    markup = flat_markup().rstrip("\n")
    escaped = (markup.replace("\\", "\\\\")
                     .replace("\n", "\\n")
                     .replace("'", "\\'"))

    src = open(html_path, encoding="utf-8").read()
    key = "'dashboard_tablet': '"
    start = src.index(key) + len(key)
    i = start
    while True:
        j = src.index("'", i)
        if src[j - 1] != "\\":
            break
        i = j + 1

    if src[start:j] == escaped:
        print("docs/sandbox.html already up to date")
        return
    open(html_path, "w", encoding="utf-8").write(
        src[:start] + escaped + src[j:])
    print(f"updated docs/sandbox.html dashboard_tablet "
          f"({j - start} -> {len(escaped)} chars)")


def main(argv=None):
    parser = argparse.ArgumentParser(
        description="Generate the Loud dashboard Incense markup.")
    parser.add_argument(
        "--split", metavar="DIR",
        help="write one root app.aroma plus smaller @embed fragments")
    parser.add_argument(
        "--update-docs", action="store_true",
        help="splice the flat markup into docs/sandbox.html in place")
    args = parser.parse_args(argv)
    if args.split:
        write_split(args.split)
    elif args.update_docs:
        update_docs_sandbox()
    else:
        sys.stdout.write(flat_markup())


if __name__ == "__main__":
    main()
