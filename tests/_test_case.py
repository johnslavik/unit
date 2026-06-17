import os
import unittest
import tempfile
from pathlib import Path
import subprocess

BUILD_DIR = os.environ.get("BUILD_DIR", "./build")


class ExampleTestRunner(unittest.TestCase):
    executable_name: str

    def __init_subclass__(cls, executable_name: str) -> None:
        cls.executable_name = executable_name
        super().__init_subclass__()

    def setUp(self) -> None:
        self.build_dir = Path(BUILD_DIR).absolute()
        if not self.build_dir.exists():
            raise FileNotFoundError(f"{self.build_dir} not found")
        self.temporary = tempfile.TemporaryDirectory()
        path = Path(self.temporary.name)
        self.obj = path / "test.o"
        self.exe = path / "test"

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def compile_and_run(
        self,
        *,
        build_args: list[str] | None = None,
        run_args: list[str] | None = None,
        build_input: str | None = None,
        run_input: str | None = None,
    ) -> str:
        subprocess.run(
            [self.build_dir / self.executable_name, *(build_args or ())],
            input=build_input,
            check=True,
            cwd=self.temporary.name,
            encoding="utf-8",
            timeout=5,
        )
        # TODO: Detect MSVC and Clang
        subprocess.run(
            ["gcc", "-o", "out", "test.o", "-lc"],
            check=True,
            cwd=self.temporary.name,
            timeout=5,
        )
        result = subprocess.run(
            [Path(self.temporary.name) / "out", *(run_args or ())],
            capture_output=True,
            input=run_input,
            encoding="utf-8",
            cwd=self.temporary.name,
            timeout=5,
        )
        assert isinstance(result.stdout, str)
        return result.stdout
