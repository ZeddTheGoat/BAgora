# SB-HUBO End-to-End Test Guide

The `scripts/test_sbhubo_e2e.py` helper orchestrates an Agora downlink/uplink
simulation where the LDPC decoder is backed by the SB-HUBO implementation.
This guide summarizes how to adjust its parameters and interpret the output.

## Running the test

```bash
python3 scripts/test_sbhubo_e2e.py [options]
```

If the specified build directory does not contain a configured CMake cache, the
script configures and builds the required executables (`data_generator`, `user`,
`chsim`, and `agora`) automatically before launching the run.

## Useful parameters

| Option | Description |
| --- | --- |
| `--max-frame` | Number of frames to simulate. Determines runtime and BER sample size. |
| `--ber-threshold` | Maximum acceptable UL BER per UE before the run is flagged as failed. |
| `--log-dir` | Directory where UE/chsim/Agora logs are written. Defaults to `files/log`. |
| `--keep-logs` | Preserve the generated log files instead of deleting them on success. |
| `--show-ber` | Print each UE’s UL bit-error summary to stdout once the run passes. |
| `--chsim-bs-threads` | Number of base-station threads for the channel simulator. |
| `--chsim-ue-threads` | Number of UE threads for the channel simulator. |
| `--chsim-worker-threads` | Number of worker threads for the channel simulator. |
| `--chsim-core-offset` | Core offset passed to the channel simulator. |

Run `python3 scripts/test_sbhubo_e2e.py --help` to see the full list, including
build-related switches such as `--build-dir` and `--generator`.

## Viewing results

During execution the script prints status lines with the `[test_sbhubo_e2e]`
prefix. When `--show-ber` is supplied, the final section includes per-UE BER
values of the form `UE <id>: <errors> errors / <bits> bits -> BER <value>`.

The detailed logs from Agora, the UE client, and the channel simulator are
written to the selected `--log-dir`. By default they are deleted after a
successful run; specify `--keep-logs` to retain them for inspection.
