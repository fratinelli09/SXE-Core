"""Runs one configuration through sxe.py like the SXE program (golden tests from Python)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Python"))
import sxe  # noqa: E402


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: sxe_run.py <EngineConfig.ini>", file=sys.stderr)
        return 1
    try:
        r = sxe.run(sys.argv[1])
    except sxe.Error as e:
        print(f"sxe_run.py: {type(e).__name__}\n{e}", file=sys.stderr)
        return 1
    print(f"{r.reason}: equity {r.final_equity:.2f} ({r.return_pct:+.2f}%), cycles {r.cycles}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
