#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import json
import random
import string
import subprocess
import sys


def run_case(cli: str, config: str, payload: str, index: int) -> None:
    command = [
        cli,
        "--json",
        "--dry-run",
        "--timeout",
        "1000",
        "--config",
        config,
        "run",
        "-",
    ]
    try:
        completed = subprocess.run(
            command,
            input=payload + "\n",
            text=True,
            capture_output=True,
            timeout=2.0,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"case {index} timed out") from error
    if completed.returncode < 0:
        raise AssertionError(f"case {index} died by signal {-completed.returncode}")
    combined = completed.stdout + completed.stderr
    if len(combined) > 262144:
        raise AssertionError(f"case {index} produced excessive output")
    for line in combined.splitlines():
        try:
            value = json.loads(line)
        except json.JSONDecodeError as error:
            raise AssertionError(f"case {index} emitted invalid JSON: {line!r}") from error
        if not isinstance(value, dict) or not isinstance(value.get("ok"), bool):
            raise AssertionError(f"case {index} emitted JSON without boolean ok")


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} GPIOCTL_ZSH CONFIG", file=sys.stderr)
        return 2
    cli, config = sys.argv[1:]
    fixed = [
        "",
        "# comment only",
        "set",
        "set GPIO0_1 -1",
        "set GPIO0_1 2",
        "blink GPIO0_1 999999999999999999999 1 1",
        "sleep -1",
        "sleep 999999999999999999999999999999",
        "transaction",
        "transaction /dev/gpio0_zsh extra",
        "tx-line 1 out 1 unexpected",
        "commit 999999999999999999999999",
        "iopad GPIO0_1 mux=func6",
        "watch GPIO0_1 sideways 1 1",
        "\"unterminated",
        "\\",
        "A" * 70000,
    ]
    for index, payload in enumerate(fixed):
        run_case(cli, config, payload, index)

    rng = random.Random(0x5A5348)
    alphabet = string.ascii_letters + string.digits + " _-+/=:#\\\"'\t"
    for index in range(len(fixed), len(fixed) + 300):
        length = rng.randrange(0, 4097)
        payload = "".join(rng.choice(alphabet) for _ in range(length))
        run_case(cli, config, payload, index)
    print(f"cli_parser_fuzz_zsh.py: PASS cases={len(fixed) + 300}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
