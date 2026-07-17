#!/usr/bin/env python3
from __future__ import annotations

import time


class StartToStartPollingCadence:
    """Pace sample starts without relative-sleep drift or catch-up bursts."""

    def __init__(self, interval_seconds, clock=time.monotonic, sleeper=time.sleep):
        self.interval_seconds = max(0.001, float(interval_seconds))
        self._clock = clock
        self._sleeper = sleeper
        self._last_sample_start = self._clock()

    def wait_next(self):
        now = self._clock()
        next_start = self._last_sample_start + self.interval_seconds
        missed_slots = 0
        if now > next_start:
            missed_slots = int((now - next_start) / self.interval_seconds) + 1
        delay = next_start - now
        if delay > 0.0:
            self._sleeper(delay)
        wake = self._clock()
        self._last_sample_start = wake
        return wake, missed_slots
