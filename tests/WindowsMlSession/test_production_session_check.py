# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import math
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


@unittest.skipUnless(sys.platform == "win32", "native Windows session harness requires Windows")
class ProductionSessionCheckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.executable = cls.required_file("WINDOWS_ML_SESSION_CHECK_EXE")
        cls.model = cls.required_file("WINDOWS_ML_SESSION_CHECK_MODEL")

    @staticmethod
    def required_file(name):
        value = os.environ.get(name)
        if not value or not Path(value).is_file():
            raise AssertionError(f"{name} must name an existing file")
        return Path(value)

    def run_check(self, *arguments):
        return subprocess.run([str(self.executable), *map(str, arguments)], capture_output=True,
                              text=True, check=False, timeout=180)

    def arguments(self, model=None):
        return ["--provider", "cpu", "--model", model or self.model,
                "--iterations", "100", "--cycles", "3", "--require-effective"]

    def test_cpu_runs_the_tracked_tensor_contract_across_three_cycles(self):
        result = self.run_check(*self.arguments())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        report = {}
        for line in result.stdout.splitlines():
            key, separator, value = line.partition("=")
            self.assertTrue(separator, line)
            self.assertNotIn(key, report)
            report[key] = value
        self.assertEqual(report["cycles"], "3")
        self.assertEqual(report["completed_cycles"], "3")
        self.assertEqual(report["windows_ml_package_version"], "2.2.12")
        self.assertTrue(report["onnxruntime_version"])
        self.assertEqual(report["status"], "ok")
        for cycle in range(3):
            prefix = f"cycle.{cycle}."
            expected = {"requested_provider": "cpu", "effective_provider": "CPUExecutionProvider",
                        "fallback_reason": "", "selected_device_id": "", "selected_vendor_id": "",
                        "input_count": "1", "output_count": "1", "input_shape": "1x144x256x3",
                        "output_shape": "1x144x256x2", "input_element_count": "110592",
                        "output_element_count": "73728", "finite_output_count": "73728",
                        "warmup_iterations": "10", "iterations": "100", "status": "ok"}
            for key, value in expected.items():
                self.assertEqual(report[prefix + key], value, prefix + key)
            for key in ("latency_average_ms", "latency_p50_ms", "latency_p95_ms"):
                value = float(report[prefix + key])
                self.assertTrue(math.isfinite(value))
                self.assertGreaterEqual(value, 0)
            self.assertEqual(float(report[prefix + "mae"]), 0)
            self.assertEqual(float(report[prefix + "iou"]), 1)

    def test_rejects_invalid_arguments_without_initializing_a_session(self):
        for arguments in ([], ["--provider", "cuda", "--model", self.model],
                          ["--provider", "cpu"], ["--model", self.model],
                          ["--provider", "cpu", "--model", ""],
                          self.arguments() + ["--unknown"],
                          self.arguments() + ["--provider", "cpu"],
                          self.arguments() + ["--require-effective"]):
            with self.subTest(arguments=arguments):
                result = self.run_check(*arguments)
                self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
        for option in ("--iterations", "--cycles"):
            for count in ("0", "-1", "one", "1.5", "101", "9999999999999999999999999"):
                arguments = self.arguments()
                arguments[arguments.index(option) + 1] = count
                with self.subTest(option=option, count=count):
                    result = self.run_check(*arguments)
                    self.assertEqual(result.returncode, 2, result.stdout + result.stderr)

    def test_missing_and_corrupt_models_fail_initialization(self):
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "mediapipe.onnx"
            for content in (None, b"", b"not an ONNX model"):
                if content is not None:
                    model.write_bytes(content)
                result = self.run_check(*self.arguments(model))
                self.assertEqual(result.returncode, 4, result.stdout + result.stderr)
                self.assertIn("status=error", result.stdout)

    def test_same_size_model_substitution_cannot_pass_the_tracked_model_gate(self):
        with tempfile.TemporaryDirectory() as directory:
            model = Path(directory) / "mediapipe.onnx"
            data = bytearray(self.model.read_bytes())
            data[-1] ^= 1
            model.write_bytes(data)
            result = self.run_check(*self.arguments(model))
            self.assertEqual(result.returncode, 4, result.stdout + result.stderr)
            self.assertIn("status=error", result.stdout)


if __name__ == "__main__":
    unittest.main()
