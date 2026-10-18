"""Native tests of the real PHY adapter, ST component and link monitor."""

from pathlib import Path
import shutil
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[2]
    compiler = shutil.which("gcc")
    if compiler is None:
        print("Native GCC is required", file=sys.stderr)
        return 1
    component = root / "Drivers/BSP/Components/lan8742"
    output = root / "Logs/M1_1/test_ethernet.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror",
             "-I", str(root / "Tests/Network/stubs"), "-I", str(root / "App"),
             "-I", str(component)]
    sources = [root / "Tests/Network/test_ethernet.c",
               root / "App/BSP/bsp_ethernet.c",
               root / "App/Network/ethernet_monitor.c", component / "lan8742.c"]
    objects = []
    for source in sources:
        obj = output.parent / (source.stem + ".o")
        # ST's unchanged ClearIT has an unused Interrupt argument. Keep our
        # own sources strict; isolate this vendor diagnostic to its object.
        vendor_flags = ["-Wno-unused-parameter"] if source.parent == component else []
        result = subprocess.run([compiler, *flags, *vendor_flags, "-c", str(source),
                                 "-o", str(obj)], cwd=root, check=False, timeout=30)
        if result.returncode != 0:
            return result.returncode
        objects.append(str(obj))
    result = subprocess.run([compiler, *objects, "-o", str(output)],
                            cwd=root, check=False, timeout=30)
    if result.returncode != 0:
        return result.returncode
    return subprocess.run([str(output)], cwd=root, check=False, timeout=30).returncode


if __name__ == "__main__":
    sys.exit(main())
