"""Expose ComfyUI's internal node progress on its normal process stream.

Newer ComfyUI releases already contain a CLI progress handler, but only the
WebUI handler is registered by default.  Minifox consumes the process streams,
so register the existing CLI handler alongside the WebUI handler without
changing ComfyUI's source tree or taking over its browser WebSocket session.
"""

import os
import runpy
import sys


def enable_cli_progress() -> None:
    try:
        from comfy_execution import progress

        original_register = progress.ProgressRegistry.register_handler
        if getattr(original_register, "_minifox_progress_bridge", False):
            return

        def register_handler(registry, handler):
            original_register(registry, handler)
            if (
                getattr(handler, "name", "") == "webui"
                and "cli" not in registry.handlers
            ):
                cli_handler = progress.CLIProgressHandler()
                cli_handler.set_registry(registry)
                original_register(registry, cli_handler)

        register_handler._minifox_progress_bridge = True
        progress.ProgressRegistry.register_handler = register_handler
    except Exception:
        # Older ComfyUI builds do not expose the handler. Their existing
        # terminal progress output remains available to Minifox unchanged.
        return


main_path = os.path.abspath(os.environ.get("MINIFOX_COMFY_MAIN", "main.py"))
comfy_root = os.path.dirname(main_path)
if comfy_root not in sys.path:
    sys.path.insert(0, comfy_root)

enable_cli_progress()
sys.argv[0] = main_path
runpy.run_path(main_path, run_name="__main__")
