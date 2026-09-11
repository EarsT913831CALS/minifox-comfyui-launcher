"""Expose ComfyUI's internal node progress on its normal process stream.

Newer ComfyUI releases already contain a CLI progress handler, but only the
WebUI handler is registered by default.  Minifox consumes the process streams,
so register a title-aware, stage-filtered CLI handler alongside the WebUI
handler without changing ComfyUI's source tree or taking over its browser
WebSocket session.
"""

import sys

# The bridge directory is launcher storage, never a Python module search root.
# Do this before importing runpy/pkgutil (works with ordinary, venv and embedded
# Python without requiring newer interpreter-specific isolation switches).
_bridge_directory = __file__.replace("\\", "/").rsplit("/", 1)[0].rstrip("/").casefold()
sys.path[:] = [p for p in sys.path
               if p.replace("\\", "/").rstrip("/").casefold() != _bridge_directory]
import os
import runpy


def enable_comfy_argument_parsing() -> None:
    """Match main.py's setup before imports can cache default CLI values."""
    try:
        import comfy.options

        comfy.options.enable_args_parsing()
    except Exception:
        # Older ComfyUI builds may not expose this module. Their main entry
        # point remains responsible for parsing the original argument list.
        return


def enable_cli_progress() -> None:
    try:
        from comfy_execution import progress

        original_register = progress.ProgressRegistry.register_handler
        if getattr(original_register, "_minifox_progress_bridge", False):
            return

        class MinifoxCLIProgressHandler(progress.CLIProgressHandler):
            """Show progress only for meaningful inference stages."""

            def __init__(self):
                super().__init__()
                self._minifox_registry = None
                self._visible_nodes = set()
                self._hidden_nodes = set()

            def set_registry(self, registry):
                super().set_registry(registry)
                self._minifox_registry = registry
                self._visible_nodes.clear()
                self._hidden_nodes.clear()

            def _node_records(self, node_id):
                registry = self._minifox_registry
                dynprompt = getattr(registry, "dynprompt", None)
                if dynprompt is None:
                    return []

                candidate_ids = [node_id]
                for method_name in ("get_display_node_id", "get_real_node_id"):
                    method = getattr(dynprompt, method_name, None)
                    if callable(method):
                        try:
                            candidate_id = method(node_id)
                        except Exception:
                            continue
                        if candidate_id not in candidate_ids:
                            candidate_ids.append(candidate_id)

                get_node = getattr(dynprompt, "get_node", None)
                if not callable(get_node):
                    return []

                records = []
                for candidate_id in candidate_ids:
                    try:
                        node = get_node(candidate_id)
                    except Exception:
                        continue
                    if not hasattr(node, "get"):
                        continue
                    metadata = node.get("_meta")
                    title = metadata.get("title") if hasattr(metadata, "get") else None
                    class_type = node.get("class_type")
                    records.append((
                        str(class_type or "").strip(),
                        str(title or "").strip(),
                    ))
                return records

            @staticmethod
            def _is_loader(class_type):
                normalized = class_type.casefold().replace("_", "").replace("-", "")
                return any(token in normalized for token in (
                    "loader",
                    "checkpoint",
                    "unet",
                    "diffusionmodel",
                ))

            def _is_core_progress_node(self, node_id):
                records = self._node_records(node_id)
                for class_type, _ in records:
                    normalized = class_type.casefold().replace("_", "").replace("-", "")
                    if self._is_loader(class_type):
                        continue
                    if any(token in normalized for token in (
                        "ksampler",
                        "samplercustom",
                        "sampler",
                        "textencode",
                        "textencoder",
                        "cliptext",
                        "vae",
                    )):
                        return True

                for _, title in records:
                    normalized = title.casefold()
                    if any(marker in normalized for marker in (
                        "\u91c7\u6837",
                        "\u6587\u672c\u7f16\u7801",
                        "text encode",
                        "text encoder",
                        "vae",
                    )) or normalized.strip() == "te":
                        return True
                return False

            def _should_track_node(self, node_id):
                if node_id in self._visible_nodes:
                    return True
                if node_id in self._hidden_nodes:
                    return False
                if self._is_core_progress_node(node_id):
                    self._visible_nodes.add(node_id)
                    return True
                self._hidden_nodes.add(node_id)
                return False

            def _node_label(self, node_id):
                fallback = ""
                for class_type, title in self._node_records(node_id):
                    if title:
                        return title
                    if not fallback and class_type:
                        fallback = class_type
                if fallback:
                    return fallback
                return f"Node {node_id}"

            def _create_progress_bar(self, node_id, total):
                bar = progress.tqdm(
                    total=total,
                    desc=self._node_label(node_id),
                    unit="steps",
                    leave=True,
                    position=len(self.progress_bars),
                )
                self.progress_bars[node_id] = bar
                return bar

            def start_handler(self, node_id, state, prompt_id):
                if not self._should_track_node(node_id):
                    return
                if node_id not in self.progress_bars:
                    self._create_progress_bar(node_id, state["max"])

            def update_handler(
                self, node_id, value, max_value, state, prompt_id, image=None
            ):
                if not self._should_track_node(node_id):
                    return
                if node_id not in self.progress_bars:
                    self._create_progress_bar(node_id, max_value).update(value)
                    return

                bar = self.progress_bars[node_id]
                if max_value != bar.total:
                    bar.total = max_value
                update_amount = value - bar.n
                if update_amount > 0:
                    bar.update(update_amount)

            def reset(self):
                super().reset()
                self._visible_nodes.clear()
                self._hidden_nodes.clear()

        def register_handler(registry, handler):
            original_register(registry, handler)
            if (
                getattr(handler, "name", "") == "webui"
                and "cli" not in registry.handlers
            ):
                cli_handler = MinifoxCLIProgressHandler()
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

enable_comfy_argument_parsing()
enable_cli_progress()
sys.argv[0] = main_path
runpy.run_path(main_path, run_name="__main__")
