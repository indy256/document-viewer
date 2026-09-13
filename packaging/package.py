"""Package a CMake install tree as a portable EXE, AppImage, or macOS disk image."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--platform", choices=["windows", "linux", "macos"], required=True)
parser.add_argument("--stage", type=Path, default=Path("stage"))
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--cmake", default="cmake")
parser.add_argument("--cxx")
parser.add_argument("--ninja")
parser.add_argument("--build-dir", type=Path, help="Separate launcher build directory for another compiler")
parser.add_argument("--appimagetool", type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parent
stage = args.stage.resolve(strict=True)
output = args.output.resolve()
output.parent.mkdir(parents=True, exist_ok=True)

def run(*command, **kwargs):
    subprocess.run([str(part) for part in command], check=True, **kwargs)

if args.platform == "windows":
    if not (stage / "bin/DocumentViewer.exe").is_file():
        raise SystemExit("Deploy the application with cmake --install before packaging.")
    build = args.build_dir.resolve() if args.build_dir else root.parent / "build/portable"
    build.mkdir(parents=True, exist_ok=True)
    payload = build / "payload.zip"
    with zipfile.ZipFile(payload, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for file in sorted(stage.rglob("*")):
            if file.is_file():
                archive.write(file, file.relative_to(stage).as_posix())
    options = []
    if args.cxx:
        options.append(f"-DCMAKE_CXX_COMPILER={args.cxx}")
    if args.ninja:
        options.append(f"-DCMAKE_MAKE_PROGRAM={args.ninja}")
    run(args.cmake, "-S", root / "windows", "-B", build, "-G", "Ninja",
        "-DCMAKE_BUILD_TYPE=MinSizeRel", f"-DPAYLOAD={payload}", *options)
    run(args.cmake, "--build", build, "--parallel", "2")
    shutil.copy2(build / "DocumentViewerPortable.exe", output)
elif args.platform == "linux":
    if args.appimagetool is None:
        raise SystemExit("--appimagetool is required on Linux")
    for name in ("AppRun", "DocumentViewer.desktop"):
        shutil.copy2(root / name, stage / name)
    shutil.copy2(root.parent / "resources/icons/DocumentViewer.png", stage / "DocumentViewer.png")
    # Remove the previous packaging icon when reusing an install tree.
    (stage / "DocumentViewer.svg").unlink(missing_ok=True)
    (stage / "AppRun").chmod(0o755)
    run(args.appimagetool.resolve(), "--appimage-extract-and-run", stage, output,
        env={**os.environ, "ARCH": "x86_64", "VERSION": "1.0"})
    output.chmod(0o755)
else:
    if not (stage / "DocumentViewer.app").is_dir():
        raise SystemExit("The deployed macOS application bundle is missing.")
    run("hdiutil", "create", "-volname", "Document Viewer", "-srcfolder", stage,
        "-ov", "-format", "UDZO", output)
print(f"Created {output} ({output.stat().st_size / 1024 / 1024:.1f} MiB)")
