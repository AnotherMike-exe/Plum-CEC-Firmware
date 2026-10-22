# SPDX-License-Identifier: MIT
"""Tests for check_timing.py. The good log is TV traffic recorded on the bench (plan step 6)."""

from pathlib import Path

import check_timing

DATA = Path(__file__).parent / "testdata"


def test_recorded_tv_traffic_passes(capsys):
    assert check_timing.main([str(DATA / "timing_good.log")]) == 0
    assert "PASS" in capsys.readouterr().out


def test_one_bad_sample_fails(capsys):
    assert check_timing.main([str(DATA / "timing_bad.log")]) == 1
    out = capsys.readouterr().out
    assert "FAIL other data1_low: 1 in 850-899 us, window 400-800 us" in out


def test_bin_on_the_window_edge():
    # 750-799 us is inside 400-800 us, 800-849 us is not
    assert check_timing.bin_in_window("data1_low", 750, False)
    assert not check_timing.bin_in_window("data1_low", 800, False)
    assert not check_timing.bin_in_window("data1_low", 350, False)


def test_overflow_and_truncated_lines_fail():
    lines = [
        "[I][cec_probe:162]: own bit_period n=1 6000+:1",
        "[I][cec_probe:162]: own data0_low n=1 1500:1 truncated",
    ]
    _, failures = check_timing.check(lines)
    assert ("own", "bit_period", 6000, 1) in failures
    assert ("own", "data0_low", None, 0) in failures


def test_owner_filter():
    lines = ["[I][cec_probe:162]: own start_period n=1 5150:1", "[I][cec_probe:162]: other start_period n=1 4500:1"]
    assert check_timing.check(lines, "other")[1] == []
    assert len(check_timing.check(lines, "own")[1]) == 1


def test_log_without_probe_lines(tmp_path):
    log = tmp_path / "empty.log"
    log.write_text("[I][wifi:1626]: Connected\n")
    assert check_timing.main([str(log)]) == 2
