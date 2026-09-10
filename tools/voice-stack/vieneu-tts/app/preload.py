"""Bake exact VieNeu model artifacts into the container image."""

from .engine import _install_huggingface_revision_pins, preload_pinned_models
from .settings import Settings


def main() -> None:
    settings = Settings.from_env()
    _install_huggingface_revision_pins(settings)
    preload_pinned_models(settings)


if __name__ == "__main__":
    main()
