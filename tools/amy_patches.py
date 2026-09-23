"""Explicitly apply or check the firmware patch on the pinned AMY checkout."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PATCHES = ROOT / "components/amy/patches"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Read-only: verify the patch is present")
    parser.add_argument("--checkout", type=Path, default=ROOT / "components/amy/amy",
                        help="AMY checkout to prepare (also useful for isolated verification)")
    args = parser.parse_args()
    checkout = args.checkout.resolve()

    def git(*arguments: str) -> str:
        return subprocess.check_output(["git", "-C", str(checkout), *arguments],
                                       text=True, timeout=60).strip()

    expected = (PATCHES / "base-revision").read_text().strip()
    if git("rev-parse", "HEAD") != expected:
        parser.error(f"AMY must be at pinned revision {expected}; checkout was not modified")
    patch = str(PATCHES / "firmware.patch")
    if args.check:
        git("apply", "--reverse", "--check", patch)
        print("PASS: AMY firmware patch is present")
    else:
        # No reset, stash, automatic reverse, or already-applied fallback.
        git("apply", "--check", patch)
        git("apply", patch)
        print("Applied AMY firmware patch; Python files were not touched")


if __name__ == "__main__":
    main()
