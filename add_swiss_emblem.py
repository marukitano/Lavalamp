#!/usr/bin/env python3
from __future__ import annotations

import json
import shutil
import sys
from pathlib import Path

ROOT = Path.cwd()
MAIN_C = ROOT / "src" / "main.c"
PACKAGE_JSON = ROOT / "package.json"
CONFIG_JS = ROOT / "src" / "pkjs" / "config.js"


def fail(message: str) -> None:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def backup(path: Path) -> None:
    backup_path = path.with_suffix(path.suffix + ".bak")
    if not backup_path.exists():
        shutil.copy2(path, backup_path)
        print(f"Backup created: {backup_path}")


def replace_once(text: str, old: str, new: str, description: str) -> str:
    count = text.count(old)
    if count == 0:
        fail(f"Could not find insertion point for: {description}")
    if count > 1:
        fail(f"Insertion point is ambiguous for: {description}")
    return text.replace(old, new, 1)


def patch_main_c() -> bool:
    if not MAIN_C.exists():
        fail(f"Missing file: {MAIN_C}")

    text = MAIN_C.read_text(encoding="utf-8")
    if "static void draw_swiss_emblem(" in text:
        print("src/main.c: already patched")
        return False

    text = replace_once(
        text,
        """typedef struct {
    uint8_t background_argb;
    uint8_t blob_argb;
    uint8_t value_argb;
    uint8_t shake_values;
} LavalampSettings;
""",
        """typedef struct {
    uint8_t background_argb;
    uint8_t blob_argb;
    uint8_t value_argb;
    uint8_t shake_values;
    uint8_t show_emblem;
} LavalampSettings;

typedef struct {
    uint8_t background_argb;
    uint8_t blob_argb;
    uint8_t value_argb;
    uint8_t shake_values;
} PreviousLavalampSettings;
""",
        "extend LavalampSettings",
    )

    text = replace_once(
        text,
        """    s_settings.value_argb = GColorWhite.argb;
    s_settings.shake_values = true;
""",
        """    s_settings.value_argb = GColorWhite.argb;
    s_settings.shake_values = true;
    s_settings.show_emblem = true;
""",
        "set default emblem visibility",
    )

    text = replace_once(
        text,
        """        if (stored_size == (int)sizeof(s_settings)) {
            persist_read_data(
                SETTINGS_PERSIST_KEY,
                &s_settings,
                sizeof(s_settings));
        } else if (stored_size == (int)sizeof(LegacyLavalampSettings)) {
""",
        """        if (stored_size == (int)sizeof(s_settings)) {
            persist_read_data(
                SETTINGS_PERSIST_KEY,
                &s_settings,
                sizeof(s_settings));
        } else if (stored_size == (int)sizeof(PreviousLavalampSettings)) {
            PreviousLavalampSettings previous_settings;

            persist_read_data(
                SETTINGS_PERSIST_KEY,
                &previous_settings,
                sizeof(previous_settings));

            s_settings.background_argb = previous_settings.background_argb;
            s_settings.blob_argb = previous_settings.blob_argb;
            s_settings.value_argb = previous_settings.value_argb;
            s_settings.shake_values = previous_settings.shake_values;
            s_settings.show_emblem = true;

            save_settings();
        } else if (stored_size == (int)sizeof(LegacyLavalampSettings)) {
""",
        "migrate previous settings",
    )

    text = replace_once(
        text,
        """            // Existing users keep the current appearance after updating.
            s_settings.shake_values = true;
            save_settings();
""",
        """            // Existing users keep the current appearance after updating.
            s_settings.shake_values = true;
            s_settings.show_emblem = true;
            save_settings();
""",
        "migrate legacy settings",
    )

    text = replace_once(
        text,
        """        changed = true;
    }

    if (changed) {
        save_settings();
        apply_settings();
    }
}
""",
        """        changed = true;
    }

    tuple = dict_find(iterator, MESSAGE_KEY_ShowEmblem);
    if (tuple) {
        s_settings.show_emblem = tuple->value->int32 != 0;
        changed = true;
    }

    if (changed) {
        save_settings();
        apply_settings();
    }
}
""",
        "receive ShowEmblem setting",
    )

    emblem_code = r'''static void draw_swiss_emblem(GContext *context)
{
    if (!s_settings.show_emblem) {
        return;
    }

    static const char *const emblem_rows[14] = {
        "..RRRRRRRRR..",
        ".RRRRWWWRRRR.",
        ".RRRRWWWRRRR.",
        ".RRRRWWWRRRR.",
        ".RWWWWWWWWWR.",
        ".RWWWWWWWWWR.",
        ".RWWWWWWWWWR.",
        ".RRRRWWWRRRR.",
        ".RRRRWWWRRRR.",
        "..RRRWWWRRR..",
        "..RRRRRRRRR..",
        "....RRRRR....",
        ".....RRR.....",
        "......R......"
    };

    const int16_t emblem_width = 13;
    const int16_t emblem_height = 14;
    const GPoint center = GPoint(
        DISPLAY_WIDTH / 2,
        DISPLAY_HEIGHT / 2 - SCALE_Y(10));

    const int16_t left = center.x - emblem_width / 2;
    const int16_t top = center.y - emblem_height / 2;

#if defined(PBL_COLOR)
    const GColor shield_color = GColorRed;
    const GColor cross_color = GColorWhite;
#else
    const GColor shield_color = s_blob_color;
    const GColor cross_color = s_value_color;
#endif

    for (int16_t row = 0; row < emblem_height; row++) {
        int16_t run_start = -1;
        char current_symbol = '.';

        for (int16_t column = 0; column <= emblem_width; column++) {
            const char symbol =
                column < emblem_width ? emblem_rows[row][column] : '.';
            const bool drawable = symbol == 'R' || symbol == 'W';

            if (drawable && run_start < 0) {
                run_start = column;
                current_symbol = symbol;
                continue;
            }

            if (drawable && run_start >= 0 && symbol == current_symbol) {
                continue;
            }

            if (run_start >= 0) {
                graphics_context_set_fill_color(
                    context,
                    current_symbol == 'W' ? cross_color : shield_color);

                graphics_fill_rect(
                    context,
                    GRect(left + run_start, top + row,
                          column - run_start, 1),
                    0,
                    GCornerNone);

                run_start = -1;
                current_symbol = '.';
            }

            if (drawable) {
                run_start = column;
                current_symbol = symbol;
            }
        }
    }
}

'''

    text = replace_once(
        text,
        "static void draw_values(GContext *context)\n",
        emblem_code + "static void draw_values(GContext *context)\n",
        "insert emblem drawing function",
    )

    text = replace_once(
        text,
        """    draw_blobs(context);
    draw_values(context);
""",
        """    draw_blobs(context);
    draw_swiss_emblem(context);
    draw_values(context);
""",
        "draw emblem",
    )

    backup(MAIN_C)
    MAIN_C.write_text(text, encoding="utf-8")
    print("Updated: src/main.c")
    return True


def patch_package_json() -> bool:
    if not PACKAGE_JSON.exists():
        fail(f"Missing file: {PACKAGE_JSON}")

    data = json.loads(PACKAGE_JSON.read_text(encoding="utf-8"))
    keys = data["pebble"]["messageKeys"]

    if "ShowEmblem" in keys:
        print("package.json: already patched")
        return False

    index = keys.index("ShakeValues") + 1 if "ShakeValues" in keys else len(keys)
    keys.insert(index, "ShowEmblem")

    backup(PACKAGE_JSON)
    PACKAGE_JSON.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print("Updated: package.json")
    return True


def patch_config_js() -> bool:
    if not CONFIG_JS.exists():
        fail(f"Missing file: {CONFIG_JS}")

    text = CONFIG_JS.read_text(encoding="utf-8")
    if '"messageKey": "ShowEmblem"' in text:
        print("src/pkjs/config.js: already patched")
        return False

    old = '''      {
        "type": "toggle",
        "messageKey": "ShakeValues",
        "defaultValue": true,
        "label": "Shake to see values",
        "description": "Show the values for a few seconds after one detected wrist shake or tap."
      }
'''

    new = '''      {
        "type": "toggle",
        "messageKey": "ShakeValues",
        "defaultValue": true,
        "label": "Shake to see values",
        "description": "Show the values for a few seconds after one detected wrist shake or tap."
      },
      {
        "type": "toggle",
        "messageKey": "ShowEmblem",
        "defaultValue": true,
        "label": "Show Swiss emblem",
        "description": "Display a small Swiss coat of arms slightly above the center."
      }
'''

    text = replace_once(text, old, new, "add Clay toggle")
    backup(CONFIG_JS)
    CONFIG_JS.write_text(text, encoding="utf-8")
    print("Updated: src/pkjs/config.js")
    return True


def main() -> None:
    print(f"Repository: {ROOT}")
    changed = patch_main_c()
    changed = patch_package_json() or changed
    changed = patch_config_js() or changed

    if changed:
        print("\nDone. Next run: pebble build")
    else:
        print("\nNothing changed; the patch is already installed.")


if __name__ == "__main__":
    main()
