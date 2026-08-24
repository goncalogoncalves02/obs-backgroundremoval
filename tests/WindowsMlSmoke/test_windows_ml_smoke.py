# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import math
import os
import re
import subprocess
import sys
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == "win32", "Windows ML smoke inference requires Windows")
class WindowsMlSmokeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = cls._required_file("WINDOWS_ML_SMOKE_EXE")
        cls.model_argument = os.environ.get("WINDOWS_ML_SMOKE_MODEL", "")
        cls.model = cls._required_file("WINDOWS_ML_SMOKE_MODEL")

    def test_rejects_invalid_arguments_with_usage_exit_code(self):
        invalid_cases = (
            ((), "--provider cpu is required"),
            (("--model", str(self.model)), "--provider cpu is required"),
            (
                ("--provider", "directml", "--model", str(self.model)),
                "unsupported provider: directml (only cpu is supported)",
            ),
            (("--provider", "cpu"), "--model <path> is required"),
            (
                ("--provider", "cpu", "--provider", "cpu", "--model", str(self.model)),
                "duplicate option: --provider",
            ),
            (
                ("--provider", "cpu", "--model", str(self.model), "--model", str(self.model)),
                "duplicate option: --model",
            ),
            (
                ("--provider", "cpu", "--model", str(self.model), "--iterations", "1"),
                "unknown option: --iterations",
            ),
        )

        for arguments, expected_error in invalid_cases:
            with self.subTest(arguments=arguments):
                result = self._run(*arguments)
                self.assertEqual(result.returncode, 2, self._diagnostic(result))
                self.assertEqual(result.stdout, "")
                self.assertEqual(
                    result.stderr,
                    f"error={expected_error}\n"
                    "usage=windows-ml-smoke --provider cpu --model <path>\n",
                )

    def test_runs_the_tracked_mediapipe_contract_once_on_cpu(self):
        result = self._run("--provider", "cpu", "--model", self.model_argument)

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(result.stderr, "")
        report = self._parse_report(result.stdout)
        self.assertEqual(
            set(report),
            {
                "status",
                "provider",
                "architecture",
                "windows_version",
                "windows_ml_package_version",
                "onnxruntime_version",
                "model",
                "input_count",
                "output_count",
                "input_name",
                "output_name",
                "input_element_type",
                "output_element_type",
                "input_shape",
                "output_shape",
                "iterations",
                "finite_output_count",
                "latency_ms",
            },
        )
        self.assertEqual(report["status"], "ok")
        self.assertEqual(report["provider"], "cpu")
        self.assertEqual(report["architecture"], "x64")
        self.assertRegex(report["windows_version"], r"^\d+\.\d+\.\d+$")
        self.assertEqual(report["windows_ml_package_version"], "2.2.12")
        self.assertTrue(report["onnxruntime_version"])
        self.assertEqual(report["model"], self.model_argument)
        self.assertEqual(report["input_count"], "1")
        self.assertEqual(report["output_count"], "1")
        self.assertTrue(report["input_name"])
        self.assertTrue(report["output_name"])
        self.assertEqual(report["input_element_type"], "float")
        self.assertEqual(report["output_element_type"], "float")
        self.assertEqual(report["input_shape"], "1x144x256x3")
        self.assertEqual(report["output_shape"], "1x144x256x2")
        self.assertEqual(report["iterations"], "1")
        self.assertEqual(report["finite_output_count"], "73728")
        latency_ms = float(report["latency_ms"])
        self.assertTrue(math.isfinite(latency_ms))
        self.assertGreaterEqual(latency_ms, 0.0)

    @classmethod
    def _run(cls, *arguments):
        return subprocess.run(
            [str(cls.executable), *arguments],
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def _parse_report(output):
        report = {}
        for line in output.splitlines():
            match = re.fullmatch(r"([^=]+)=(.*)", line)
            if not match:
                raise AssertionError(f"diagnostic is not key=value: {line!r}")
            key, value = match.groups()
            if key in report:
                raise AssertionError(f"duplicate diagnostic key: {key}")
            report[key] = value
        return report

    @staticmethod
    def _required_file(environment_name):
        value = os.environ.get(environment_name)
        if not value:
            raise AssertionError(f"{environment_name} must name an existing file")
        path = Path(value)
        if not path.is_file():
            raise AssertionError(f"{environment_name} does not name a file: {path}")
        return path

    @staticmethod
    def _diagnostic(result):
        return f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"


if __name__ == "__main__":
    unittest.main()
