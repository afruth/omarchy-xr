"""Clocks shared by the viewer, the SDK worker, and the Hyprland adapter."""
import time


def boot_time():
    """Seconds since boot, including time spent suspended. Matches /proc/uptime."""
    return time.clock_gettime(time.CLOCK_BOOTTIME)
