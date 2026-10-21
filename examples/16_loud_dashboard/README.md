# Loud Dashboard

A five-page analytics dashboard built entirely with AromaUI widgets and
Incense markup. Nothing on screen is decorative: every pill, toggle, menu,
table row and chart control drives the runtime in `src/loud_dashboard.c`.

| Page | What it does |
| --- | --- |
| Dashboard | Period revenue, stat deltas, income/expense chart, budget donut, recent transactions |
| Analytics | Product breakdown, weekly deliveries, activity heatmap (click a day for details) |
| Transactions | Searchable/filterable table, transaction detail, mark reviewed, CSV export |
| Reports | Statement and category exports, period comparison, ready-made CSV downloads |
| Settings | Profile name, notification toggles, privacy mask, reset demo data, sign out |

Extras: global timeframe control, command palette style search, notification
inbox, profile menu, transfer/request dialogs with validation, snackbars, and
real CSV downloads (via Blob in the browser, files on desktop).

## Run it

```bash
cd examples/16_loud_dashboard
python3 ../bin/aroma run linux
```

Web and Android follow the standard example layout (`aroma build web`,
`aroma build android`).

## Source layout

- `ui/app.aroma` - the window root. It `@embed`s one generated fragment per
  screen section (`ui/pages/`, `ui/overlays/`), passing the props each
  fragment reads. Regenerate the whole tree from the generator instead of
  editing it:

  ```bash
  # the example app (root + one file per section)
  python3 tools/sandbox/gen_dashboard.py --split examples/16_loud_dashboard/ui

  # the flattened markup the docs live demo embeds
  python3 tools/sandbox/gen_dashboard.py --update-docs
  ```

- `src/loud_dashboard.c` - application state and every callback. The same
  file powers the live demo on the docs page, so an improvement here lands
  in both places.
- `src/main.c` - registers the font sizes the markup asks for, binds them to
  the dashboard runtime, registers callbacks, and mounts the window.
- `assets/` - logo and avatar used by the profile and signed-out screens.
