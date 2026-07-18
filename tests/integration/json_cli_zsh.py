#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Validate that every --json output line is independent, parseable JSON."""

import json
import errno
import os
import subprocess
import sys
import tempfile
import time


def parse_lines(text: str, channel: str) -> list[dict]:
    records = []
    for number, line in enumerate(text.splitlines(), 1):
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError as error:
            raise AssertionError(
                f"{channel} line {number} is not JSON: {line!r}"
            ) from error
        if not isinstance(record, dict) or "ok" not in record:
            raise AssertionError(f"{channel} line {number} lacks an ok field")
        records.append(record)
    return records


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} CLI CONFIG", file=sys.stderr)
        return 2
    cli, config = sys.argv[1:]
    base = [cli, "--json", "--config", config]

    def invoke(
        arguments: list[str],
        *,
        stdin: str | None = None,
        expected: int = 0,
        options: list[str] | None = None,
    ) -> tuple[list[dict], list[dict]]:
        command = base + (options or []) + arguments
        result = subprocess.run(
            command,
            input=stdin,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if result.returncode != expected:
            raise AssertionError(
                f"{command!r}: rc={result.returncode}, expected={expected}, "
                f"stdout={result.stdout!r}, stderr={result.stderr!r}"
            )
        stdout = parse_lines(result.stdout, "stdout")
        stderr = parse_lines(result.stderr, "stderr")
        if expected == 0:
            if not stdout or stderr or not all(item["ok"] is True for item in stdout):
                raise AssertionError(f"unexpected success records for {command!r}")
        elif not stderr or not all(item["ok"] is False for item in stderr):
            raise AssertionError(f"unexpected error records for {command!r}")
        return stdout, stderr

    invoke(["list"])
    invoke(["resolve", "GPIO1_11"])
    info, _ = invoke(["info", "/dev/gpio0_zsh:10"])
    if len(info) != 1 or info[0].get("operation") != "info":
        raise AssertionError("info must emit exactly one JSON object")
    invoke(["get", "/dev/gpio0_zsh:10"])
    invoke(["set", "/dev/gpio0_zsh:10", "1", "10"], options=["--dry-run"])
    invoke(
        ["blink", "/dev/gpio0_zsh:10", "2", "10", "10"],
        options=["--dry-run"],
    )
    invoke(
        [
            "pair-blink",
            "/dev/gpio0_zsh:10",
            "/dev/gpio0_zsh:11",
            "2",
            "10",
        ],
        options=["--dry-run"],
    )
    invoke(
        ["batch-set", "/dev/gpio0_zsh", "10", "10=1", "11=0"],
        options=["--dry-run"],
    )
    invoke(["iopad-get", "/dev/gpio0_zsh:10"])
    invoke(
        ["iopad", "/dev/gpio0_zsh:10", "bias=up", "drive=3", "mux=gpio"],
        options=["--dry-run"],
    )
    invoke(["stats", "/dev/gpio0_zsh"])

    script = "\n".join(
        [
            "acquire /dev/gpio0_zsh:10 out 0",
            "value /dev/gpio0_zsh:10 1",
            "value /dev/gpio0_zsh:10",
            "release /dev/gpio0_zsh:10",
            "sleep 0",
            "transaction /dev/gpio0_zsh",
            "tx-line 10 out 1",
            "tx-line 11 out 0",
            "commit 0",
            "help",
            "quit",
            "",
        ]
    )
    invoke(["run", "-"], stdin=script, options=["--strict"])
    shell, _ = invoke(["shell"], stdin="help\nquit\n")
    if any("gpioctl_zsh>" in json.dumps(item) for item in shell):
        raise AssertionError("JSON shell leaked a human prompt")

    escaped = 'bad"target\\name'
    _, errors = invoke(["get", escaped], expected=1)
    if not any(item.get("subject") == escaped for item in errors):
        raise AssertionError("JSON string escaping did not round-trip")

    start = time.monotonic()
    invoke(["sleep", "200"], expected=1, options=["--timeout", "20"])
    if time.monotonic() - start > 1.0:
        raise AssertionError("global timeout did not bound sleep")
    invoke(["sleep", "86400001"], expected=1)

    with tempfile.NamedTemporaryFile("w", delete=False, encoding="utf-8") as stream:
        duplicate_config = stream.name
        stream.write("dup /dev/gpio0_zsh 1 0 PAD 1 first\n")
        stream.write("dup /dev/gpio0_zsh 2 0 PAD 2 second\n")
    try:
        _, errors = invoke(
            ["resolve", "dup"],
            expected=1,
            options=["--config", duplicate_config],
        )
        if not any(item.get("errno") == errno.EEXIST for item in errors):
            raise AssertionError("duplicate alias did not report EEXIST")
    finally:
        os.unlink(duplicate_config)

    print("json_cli_zsh.py: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
