#!/usr/bin/env python3
"""Focused source contract for the settings model-mapping confirmation flow."""

from pathlib import Path


BOARD_ROOT = Path(__file__).resolve().parents[2]
WIDGETS = BOARD_ROOT / "app" / "src" / "v5_settings_page_widgets.c"
STATUS = BOARD_ROOT / "app" / "src" / "v5_settings_page_status.c"
HEADER = BOARD_ROOT / "app" / "src" / "v5_settings_page.h"


def function_body(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def main() -> int:
    widgets = WIDGETS.read_text(encoding="utf-8")
    status = STATUS.read_text(encoding="utf-8")
    header = HEADER.read_text(encoding="utf-8")

    changed = function_body(widgets, "static void settings_motion_model_changed_cb")
    assert '"motion_model_mapping_confirm"' in changed
    assert "默认映射已自动更新" in changed
    assert "轴参数 → 从站" in changed
    assert "v5_settings_page_trigger_action" not in changed

    close = function_body(status, "static void settings_popup_close_cb")
    prompt_branch = close.index("popup_model_mapping_set_drive")
    generic_final = close.index("if (page->popup_final)", prompt_branch + 1)
    trigger = close.index("V5_MAIN_PAGE_ACTION_SETTINGS_SET_DRIVE", prompt_branch)
    assert prompt_branch < trigger < generic_final
    assert 'model_mapping_set_drive ? "设置驱动" : "关闭"' in status
    assert "int popup_model_mapping_set_drive;" in header

    print("V5_SETTINGS_MODEL_MAPPING_PROMPT_OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
