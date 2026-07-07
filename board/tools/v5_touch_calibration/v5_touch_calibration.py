#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys

from v5_touch_core import *  # noqa: F401,F403
from v5_touch_evdev import *  # noqa: F401,F403
from v5_touch_window import TouchCalibrationWindow


def main():
    app = QtWidgets.QApplication(sys.argv)
    family = _install_runtime_font(app)
    print(f"[touch-cal] ui font family={family}", file=sys.stderr)
    window = TouchCalibrationWindow()
    window.showFullScreen()
    sys.exit(app.exec_())


if __name__ == "__main__":
    main()

