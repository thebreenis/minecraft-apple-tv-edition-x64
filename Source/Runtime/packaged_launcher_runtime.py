"""Defaults injected only into the packaged Apple TV Edition launcher."""

import sys
import os
import startup_diagnostics
startup_diagnostics.event('packaged_launcher_bootstrap')
def _diagnostic_exception(kind, value, traceback):
    startup_diagnostics.event('bootstrap_exception', error_type=kind.__name__)
sys.excepthook = _diagnostic_exception

# Windowed PyInstaller has no stdio. Discard diagnostics rather than opening a
# console or persisting authentication output. User action uses explicit alerts.
if sys.stdout is None:sys.stdout=open(os.devnull,'w')
if sys.stderr is None:sys.stderr=open(os.devnull,'w')


if "--launch" not in sys.argv:
    sys.argv.append("--launch")
if "--browser-sign-in" not in sys.argv:
    sys.argv.append("--browser-sign-in")
if "--exit-with-game" not in sys.argv:
    sys.argv.append("--exit-with-game")
