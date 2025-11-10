#!/usr/bin/env python3
"""Run the SB-HUBO-backed Agora end-to-end simulation and verify BER."""

from __future__ import annotations

import argparse
import os
import re
import signal
import subprocess
import sys
import time
from pathlib import Path
from typing import Dict, Iterable, Tuple, TextIO


LOG_PREFIX = "[test_sbhubo_e2e]"
REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_BUILD_DIR = REPO_ROOT / "build"
CONFIG_TEMPLATE = REPO_ROOT / "files" / "config" / "ci" / "chsim.json"
LOG_DIR = REPO_ROOT / "files" / "log"


class CommandError(RuntimeError):
    pass


def log(msg: str) -> None:
    print(f"{LOG_PREFIX} {msg}")


def run(cmd: Iterable[str], *, cwd: Path | None = None) -> None:
    """Run *cmd* synchronously, raising CommandError on failure."""
    cmd_list = [str(part) for part in cmd]
    log("$ " + " ".join(cmd_list))
    run_cwd = str(cwd) if cwd is not None else None
    result = subprocess.run(cmd_list, cwd=run_cwd, check=False)
    if result.returncode != 0:
        raise CommandError(
            f"Command {' '.join(cmd_list)} failed with exit code {result.returncode}"
        )


def ensure_configured(build_dir: Path, generator: str | None) -> None:
    """Configure CMake in *build_dir* if no cache is present."""
    cache = build_dir / "CMakeCache.txt"
    if cache.exists():
        return
    build_dir.mkdir(parents=True, exist_ok=True)
    cmd = ["cmake", "-S", REPO_ROOT, "-B", build_dir]
    if generator:
        cmd.extend(["-G", generator])
    run(cmd)


def build_targets(build_dir: Path) -> None:
    """Ensure the executables needed for the simulation are built."""
    run([
        "cmake",
        "--build",
        build_dir,
        "--target",
        "data_generator",
        "user",
        "chsim",
        "agora",
    ])


def inject_max_frame(config_text: str, max_frame: int) -> str:
    """Insert or replace the max_frame field in *config_text*."""
    pattern = re.compile(r"^(\s*\"max_frame\"\s*:\s*)(\d+)(\s*,\s*)$", re.MULTILINE)
    if pattern.search(config_text):
        return pattern.sub(rf"\g<1>{max_frame}\3", config_text)
    lines = config_text.splitlines()
    insertion = f"  \"max_frame\": {max_frame},"
    if len(lines) >= 2 and lines[0].strip() == "{":
        rebuilt = [lines[0], insertion, *lines[1:]]
        if config_text.endswith("\n"):
            rebuilt.append("")
        return "\n".join(rebuilt)
    # Fallback: prepend after stripping leading whitespace
    return insertion + "\n" + config_text


def prepare_config(max_frame: int) -> Tuple[Path, int]:
    """Create a temporary config with the desired max_frame and return UE count."""
    original = CONFIG_TEMPLATE.read_text()
    ue_match = re.search(r"\"ue_radio_num\"\s*:\s*(\d+)", original)
    if not ue_match:
        raise CommandError("Unable to locate ue_radio_num in configuration template")
    ue_count = int(ue_match.group(1))
    modified = inject_max_frame(original, max_frame)

    timestamp = int(time.time())
    tmp_path = CONFIG_TEMPLATE.with_name(f"chsim-sbhubo-{timestamp}.json")
    tmp_path.write_text(modified)
    return tmp_path, ue_count


def launch_process(
    cmd: Iterable[str], log_path: Path, env: Dict[str, str]
) -> tuple[subprocess.Popen, TextIO]:
    cmd_list = [str(part) for part in cmd]
    log(f"$ {' '.join(cmd_list)} > {log_path}")
    log_path.parent.mkdir(parents=True, exist_ok=True)
    handle = open(log_path, "w", encoding="utf-8")
    proc = subprocess.Popen(
        cmd_list,
        cwd=REPO_ROOT,
        stdout=handle,
        stderr=subprocess.STDOUT,
        env=env,
        text=True,
    )
    return proc, handle


def terminate_process(proc: subprocess.Popen, *, graceful: bool = True) -> None:
    if proc.poll() is not None:
        return
    if graceful:
        proc.send_signal(signal.SIGINT)
        try:
            proc.wait(timeout=10)
            return
        except subprocess.TimeoutExpired:
            pass
    proc.kill()
    proc.wait(timeout=10)


def extract_ul_records(log_path: Path) -> Dict[int, Tuple[int, int, float]]:
    pattern = re.compile(
        r"UE\s+(\d+):\s+UL bit errors \(BER\)\s+(\d+)/(\d+)\s+\(([0-9.eE+-]+)\)"
    )
    records: Dict[int, Tuple[int, int, float]] = {}
    for line in log_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.search(line)
        if not match:
            continue
        ue = int(match.group(1))
        errors = int(match.group(2))
        total = int(match.group(3))
        ber = float(match.group(4))
        records[ue] = (errors, total, ber)
    return records


def validate_results(
    agora_log: Path,
    user_log: Path,
    ue_count: int,
    threshold: float,
) -> None:
    agora_records = extract_ul_records(agora_log)
    user_records = extract_ul_records(user_log)

    if len(agora_records) != ue_count:
        raise CommandError(
            f"Expected {ue_count} UL BER records in Agora log, found {len(agora_records)}"
        )
    if len(user_records) != ue_count:
        raise CommandError(
            f"Expected {ue_count} UL BER records in UE log, found {len(user_records)}"
        )

    for ue in range(ue_count):
        if ue not in agora_records:
            raise CommandError(f"Missing UE {ue} record in Agora log")
        if ue not in user_records:
            raise CommandError(f"Missing UE {ue} record in UE log")

        for source, (errors, total, ber) in (
            ("Agora", agora_records[ue]),
            ("UE", user_records[ue]),
        ):
            if total == 0:
                raise CommandError(f"{source} reported zero decoded bits for UE {ue}")
            if ber > threshold:
                raise CommandError(
                    f"{source} BER {ber:.6g} for UE {ue} exceeds threshold {threshold:.6g}"
                )
            if errors != 0:
                raise CommandError(
                    f"{source} reported {errors} bit errors for UE {ue} (expected 0)"
                )


def main(argv: Iterable[str]) -> int:
    parser = argparse.ArgumentParser(
        description="Build and run the SB-HUBO-backed Agora end-to-end simulation"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=DEFAULT_BUILD_DIR,
        help="Out-of-source build directory (default: %(default)s)",
    )
    parser.add_argument(
        "--generator",
        default=None,
        help="Optional CMake generator to use when configuring the build directory",
    )
    parser.add_argument(
        "--max-frame",
        type=int,
        default=200,
        help="Number of frames to simulate (default: %(default)s)",
    )
    parser.add_argument(
        "--ber-threshold",
        type=float,
        default=5e-3,
        help="Fail if BER exceeds this threshold (default: %(default)s)",
    )
    parser.add_argument(
        "--keep-logs",
        action="store_true",
        help="Preserve generated log files instead of deleting them",
    )
    args = parser.parse_args(list(argv))

    build_dir = args.build_dir.resolve()
    tmp_config: Path | None = None
    try:
        ensure_configured(build_dir, args.generator)
        build_targets(build_dir)

        tmp_config, ue_count = prepare_config(args.max_frame)
        log(f"Using temporary config {tmp_config} with ue_count={ue_count}")

        env = os.environ.copy()
        env.setdefault("AGORA_LOG_LEVEL", "info")

        LOG_DIR.mkdir(parents=True, exist_ok=True)
        user_log = LOG_DIR / "sbhubo_user.log"
        chsim_log = LOG_DIR / "sbhubo_chsim.log"
        agora_log = LOG_DIR / "sbhubo_agora.log"

        processes: list[subprocess.Popen] = []
        log_handles: list[TextIO] = []
        try:
            data_generator_cmd = [
                build_dir / "data_generator",
                "--conf_file",
                tmp_config,
            ]
            run(data_generator_cmd, cwd=REPO_ROOT)

            env["AGORA_LOG_ENABLE"] = "1"

            user_proc, user_handle = launch_process(
                [build_dir / "user", "--conf_file", tmp_config],
                user_log,
                env,
            )
            processes.append(user_proc)
            log_handles.append(user_handle)

            time.sleep(1.0)
            chsim_proc, chsim_handle = launch_process(
                [
                    build_dir / "chsim",
                    "--bs_threads",
                    "1",
                    "--ue_threads",
                    "1",
                    "--worker_threads",
                    "4",
                    "--core_offset",
                    "19",
                    "--conf_file",
                    tmp_config,
                ],
                chsim_log,
                env,
            )
            processes.append(chsim_proc)
            log_handles.append(chsim_handle)

            time.sleep(1.0)
            agora_proc, agora_handle = launch_process(
                [build_dir / "agora", "--conf_file", tmp_config],
                agora_log,
                env,
            )
            processes.append(agora_proc)
            log_handles.append(agora_handle)

            agora_timeout = args.max_frame * 0.2 + 60
            try:
                agora_proc.wait(timeout=agora_timeout)
            except subprocess.TimeoutExpired as err:
                raise CommandError(
                    f"Agora simulation did not finish within {agora_timeout:.1f}s"
                ) from err

            user_timeout = 30
            try:
                user_proc.wait(timeout=user_timeout)
            except subprocess.TimeoutExpired as err:
                raise CommandError("UE application did not terminate as expected") from err

            terminate_process(chsim_proc)

            if agora_proc.returncode != 0:
                raise CommandError(f"Agora exited with code {agora_proc.returncode}")
            if user_proc.returncode != 0:
                raise CommandError(f"UE exited with code {user_proc.returncode}")

            for handle in log_handles:
                handle.flush()

            validate_results(agora_log, user_log, ue_count, args.ber_threshold)
            log("SB-HUBO end-to-end simulation passed BER checks")
        finally:
            for proc in processes:
                terminate_process(proc, graceful=False)
            for handle in log_handles:
                try:
                    handle.close()
                except Exception:
                    pass
        if not args.keep_logs:
            for path in (user_log, chsim_log, agora_log):
                if path.exists():
                    path.unlink()
    except CommandError as err:
        log(str(err))
        return 1
    finally:
        if tmp_config is not None:
            tmp_config.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
