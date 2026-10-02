# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REQUIRED_CPU_CONTRIB_REGISTRATIONS = (
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSDomain, 1, float, FusedConv)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, AveragePool)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, Conv)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, GlobalAveragePool)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, MaxPool)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, ReorderInput)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, ReorderOutput)>",
    "BuildKernelCreateInfo<ONNX_OPERATOR_TYPED_KERNEL_CLASS_NAME("
    "kCpuExecutionProvider, kMSNchwcDomain, 1, float, Upsample)>",
)


class ReducedOperatorsTest(unittest.TestCase):
    def test_removing_any_required_config_entry_loses_an_active_cpu_contrib_kernel_registration(self):
        repository_root = Path(__file__).resolve().parents[2]
        ort_root = Path(os.environ.get("ORT_SOURCE_DIR", repository_root / "vendor" / "onnxruntime"))
        reducer = ort_root / "tools" / "ci_build" / "reduce_op_kernels.py"
        config = repository_root / "src" / "required_operators.config"
        generated_build_dir = os.environ.get("REDUCED_OPERATORS_BUILD_DIR")

        self.assertTrue(reducer.is_file(), f"ONNX Runtime reducer is missing: {reducer}")

        if generated_build_dir:
            generated = (
                Path(generated_build_dir)
                / "op_reduction.generated"
                / "onnxruntime"
                / "contrib_ops"
                / "cpu"
                / "cpu_contrib_kernels.cc"
            )
        else:
            temporary_build_dir = tempfile.TemporaryDirectory()
            self.addCleanup(temporary_build_dir.cleanup)
            build_dir = temporary_build_dir.name
            subprocess.run(
                [
                    sys.executable,
                    str(reducer),
                    str(config),
                    "--cmake_build_dir",
                    build_dir,
                    "--is_extended_minimal_build_or_higher",
                ],
                check=True,
                cwd=repository_root,
                text=True,
                capture_output=True,
            )
            generated = (
                Path(build_dir)
                / "op_reduction.generated"
                / "onnxruntime"
                / "contrib_ops"
                / "cpu"
                / "cpu_contrib_kernels.cc"
            )
        self.assertTrue(generated.is_file(), f"Reduced CPU contrib registrations are missing: {generated}")
        active_source = "\n".join(
            line for line in generated.read_text().splitlines() if not line.lstrip().startswith("//")
        )

        for registration in REQUIRED_CPU_CONTRIB_REGISTRATIONS:
            with self.subTest(registration=registration):
                self.assertIn(registration, active_source)


if __name__ == "__main__":
    unittest.main()
