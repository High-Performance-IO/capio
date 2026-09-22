#!/usr/bin/env python3
import argparse
import json
import os
import re
import select
import shutil
import subprocess
import sys
import tarfile
import tempfile
import termios
import tty
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass
class Dependency:
    kind: str
    name: str
    url: str
    ref: str
    source: Optional[Path] = None
    cmake_args: tuple = ()

    @property
    def normalized(self):
        return re.sub(r"[^a-z0-9_.-]+", "_", self.name.lower())

    @property
    def variable(self):
        return re.sub(r"[^A-Z0-9_]+", "_", self.name.upper())


def prompt_choice(prompt, choices, default):
    if not sys.stdin.isatty():
        print(f"{prompt}{default} (non-interactive default)")
        return default
    while True:
        try:
            answer = input(prompt).strip().lower()
        except EOFError:
            answer = ""
        if not answer:
            return default
        for value, aliases in choices.items():
            if answer in aliases:
                return value
        print(f"Invalid answer. Enter {' or '.join('/'.join(a) for a in choices.values())}.")


def prompt_yes_no(prompt, default=False):
    suffix = " [Y/n]: " if default else " [y/N]: "
    return prompt_choice(prompt + suffix, {True: ("y", "yes"), False: ("n", "no")}, default)


def selector(label, choices, selected=0, highlight=True):
    def render(redraw=False):
        if redraw:
            sys.stdout.write("\033[2A")
        rows = [label]
        for index, choice in enumerate(choices):
            active = index == selected
            text = f"> {choice}" if active else f"  {choice}"
            if active and highlight:
                text = f"\033[7m{text}\033[0m"
            rows.append(text)
        sys.stdout.write("\n".join(f"\r\033[2K{row}" for row in rows))
        sys.stdout.flush()

    render()
    while True:
        key = os.read(sys.stdin.fileno(), 1)
        if key == b"\x03":
            raise KeyboardInterrupt
        if key in (b"\r", b"\n"):
            print()
            return choices[selected]
        if key == b"\x1b":
            sequence = b""
            while len(sequence) < 2 and select.select([sys.stdin], [], [], 0.05)[0]:
                sequence += os.read(sys.stdin.fileno(), 2 - len(sequence))
            if sequence in (b"[A", b"[D"):
                selected = (selected - 1) % len(choices)
            elif sequence in (b"[B", b"[C"):
                selected = (selected + 1) % len(choices)
            else:
                continue
            render(redraw=True)


def select_configuration():
    defaults = ("Release", "No Tests", "No Logger")
    if not (sys.stdin.isatty() and sys.stdout.isatty()):
        print("Configuration selectors: non-interactive input; using safe defaults.")
        return defaults

    fd = sys.stdin.fileno()
    settings = termios.tcgetattr(fd)
    print("Use Up/Down (or Left/Right) and Enter. CAPIO logging activates only in Debug builds.")
    try:
        tty.setraw(fd)
        sys.stdout.write("\033[?25l")
        sys.stdout.flush()
        highlight = os.environ.get("TERM", "") != "dumb"
        return (selector("Build", ("Release", "Debug"), highlight=highlight),
                selector("Tests", ("No Tests", "Tests"), highlight=highlight),
                selector("Logger", ("No Logger", "Logger"), highlight=highlight))
    finally:
        try:
            termios.tcsetattr(fd, termios.TCSADRAIN, settings)
        finally:
            sys.stdout.write("\033[?25h")
            sys.stdout.flush()


def run(*command, cwd=None, capture=False, check=True):
    result = subprocess.run(command, cwd=cwd, check=False, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if check and result.returncode:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"command failed ({' '.join(map(str, command))}): {detail}")
    return result


def fetch(url, ref, destination, name=None):
    name = name or destination.name
    print(f"[fetch] {name}: fetching and checking out {ref}")
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        run("git", "init", "-q", str(destination))
        run("git", "remote", "add", "origin", url, cwd=destination)
        shallow = run("git", "fetch", "-q", "--depth", "1", "origin", ref,
                      cwd=destination, capture=True, check=False)
        if shallow.returncode == 0:
            commit = "FETCH_HEAD"
        else:
            shutil.rmtree(destination)
            run("git", "init", "-q", str(destination))
            run("git", "remote", "add", "origin", url, cwd=destination)
            run("git", "fetch", "-q", "origin", "+refs/heads/*:refs/remotes/origin/*",
                "+refs/tags/*:refs/tags/*", cwd=destination)
            commit = next((candidate for candidate in (ref, f"origin/{ref}")
                           if run("git", "rev-parse", "--verify", f"{candidate}^{{commit}}",
                                  cwd=destination, capture=True, check=False).returncode == 0), None)
            if commit is None:
                raise RuntimeError(f"ref {ref!r} from {url} could not be resolved")
        run("git", "checkout", "-q", "--detach", commit, cwd=destination)
    except (RuntimeError, OSError) as error:
        raise RuntimeError(f"fetch/check-out failed for dependency {name}: {error}") from None


def declaration(record, kind):
    args = [part for argument in record.get("args", []) for part in argument.split(";")]
    if not args:
        return None
    values = {}
    for index, value in enumerate(args[1:], 1):
        key = value.upper()
        if key in {"GIT_REPOSITORY", "GIT_TAG", "URL", "SOURCE_DIR", "DOWNLOAD_COMMAND",
                   "SVN_REPOSITORY", "HG_REPOSITORY", "CVS_REPOSITORY"}:
            values[key] = args[index + 1] if index + 1 < len(args) else ""
    name = args[0]
    url = values.get("GIT_REPOSITORY")
    if not url:
        metadata = next((key for key in ("URL", "SOURCE_DIR", "DOWNLOAD_COMMAND", "SVN_REPOSITORY",
                                         "HG_REPOSITORY", "CVS_REPOSITORY") if key in values), "metadata")
        raise RuntimeError(f"{kind} dependency {name!r} uses unsupported non-Git {metadata}")
    cmake_args = ()
    if kind == "external" and "CMAKE_ARGS" in args:
        start = args.index("CMAKE_ARGS") + 1
        cmake_args = tuple(value for value in args[start:] if value.startswith("-D"))
    return Dependency(kind, name, url, values.get("GIT_TAG", "HEAD"), cmake_args=cmake_args)


def read_trace(path):
    if not path.is_file():
        raise RuntimeError("CMake JSON tracing is unavailable; CMake with --trace-format=json-v1 is required")
    dependencies = []
    for line in path.read_text().splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        command = record.get("cmd", "").lower()
        if command == "fetchcontent_declare":
            dependencies.append(declaration(record, "fetchcontent"))
        elif command == "externalproject_add" and "/Modules/" not in record.get("file", ""):
            dependencies.append(declaration(record, "external"))
    return [dependency for dependency in dependencies if dependency]


def configure_trace(source, build, definitions, cmake_args=()):
    print(f"[discovery] Configuring CMake discovery for {source.name}")
    build.parent.mkdir(parents=True, exist_ok=True)
    trace = build.parent / f"{build.name}-trace.json"
    command = ["cmake", "-Wno-dev", "-Wno-deprecated", "--trace-expand",
               "--trace-format=json-v1", f"--trace-redirect={trace}",
               "-S", str(source), "-B", str(build), "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
    command.extend(f"-D{key}={value}" for key, value in definitions.items())
    command.extend(arg.replace("<INSTALL_DIR>", str(build.parent / "install")) for arg in cmake_args)
    result = run(*command, capture=True, check=False)
    dependencies = read_trace(trace)
    if result.returncode:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"CMake dependency discovery failed for {source}: {detail}")
    print(f"[discovery] Found {len(dependencies)} declaration(s)")
    for dependency in dependencies:
        print(f"  - {dependency.name}: kind={dependency.kind}, repo={dependency.url}, ref={dependency.ref}")
    return dependencies


def merge(dependencies, found):
    for dependency in found:
        key = (dependency.kind, dependency.normalized)
        previous = dependencies.get(key)
        if previous and (previous.url, previous.ref) != (dependency.url, dependency.ref):
            raise RuntimeError(f"conflicting declarations for {dependency.name}: "
                               f"{previous.url}@{previous.ref} and {dependency.url}@{dependency.ref}")
        dependencies.setdefault(key, dependency)


def nested_dependency_projects(source):
    excluded = {".git", "_deps", "cmakefiles", "build", "_build", "generated"}
    candidates = []
    for directory, names, files in os.walk(source):
        names[:] = [name for name in names if name.lower() not in excluded
                    and not name.lower().startswith(("build-", "cmake-build-"))]
        path = Path(directory)
        if path == source or "CMakeLists.txt" not in files:
            continue
        text = (path / "CMakeLists.txt").read_text(errors="replace")
        if (re.search(r"\bproject\s*\(", text, re.IGNORECASE)
                and re.search(r"\b(?:FetchContent_Declare|ExternalProject_Add)\s*\(",
                              text, re.IGNORECASE)):
            candidates.append(path)
    return candidates


def index_populated_sources(work, dependencies, registry, override_sources):
    for key, dependency in dependencies.items():
        if dependency.kind != "fetchcontent":
            continue
        override = override_sources / f"{dependency.normalized}-src"
        if (override / ".git").exists():
            registry[key] = override
            continue
        if key in registry:
            continue
        registry[key] = next((path for path in work.rglob(f"{dependency.normalized}-src")
                              if path.is_dir() and (path / ".git").exists()), None)
        if registry[key] is None:
            del registry[key]


def discover(root, work, build_type, include_tests, overrides, known):
    dependencies = {}
    source_registry = {}
    override_sources = work / "overrides"
    definitions = {
        "CAPIO_DEPENDENCY_DISCOVERY": "ON",
        "CAPIO_BUILD_TESTS": "ON" if include_tests else "OFF",
        "CMAKE_BUILD_TYPE": build_type,
        "FETCHCONTENT_BASE_DIR": str(work / "root-deps"),
        "CALF_TESTS": "OFF",
        "CALF_PYTHON_TESTS": "OFF",
        "CALF_BUILD_PYTHON_BINDINGS": "OFF",
        "CALF_PROTOBUF_FORCE_FETCH": "ON",
        "protobuf_BUILD_TESTS": "OFF",
        "protobuf_FORCE_FETCH_DEPENDENCIES": "ON",
        "ABSL_BUILD_TESTING": "OFF",
    }
    print(f"[overrides] Applying {len(overrides)} dependency override(s)")
    for key, ref in overrides.items():
        dependency = known.get(key)
        if not dependency or dependency.kind != "fetchcontent":
            continue
        source = override_sources / f"{dependency.normalized}-src"
        fetch(dependency.url, ref, source, dependency.name)
        source_registry[key] = source
        definitions[f"FETCHCONTENT_SOURCE_DIR_{dependency.variable}"] = str(source)

    found = configure_trace(root, work / "root-build", definitions)
    merge(dependencies, found)
    index_populated_sources(work, dependencies, source_registry, override_sources)
    external_queue = [dependency for dependency in found if dependency.kind == "external"]
    configured = set()
    configured_paths = set()
    nested_index = 0
    while external_queue:
        dependency = external_queue.pop(0)
        key = (dependency.kind, dependency.normalized)
        if key in configured:
            continue
        configured.add(key)
        dependency = dependencies[key]
        ref = overrides.get(key, dependency.ref)
        source = work / "external" / dependency.normalized
        fetch(dependency.url, ref, source, dependency.name)
        dependency.source = source
        source_registry[key] = source
        configured_paths.add(source.resolve())
        nested_definitions = {
            "CMAKE_BUILD_TYPE": build_type,
            "FETCHCONTENT_BASE_DIR": str(work / "external-deps" / dependency.normalized),
        }
        for override_key, override_ref in overrides.items():
            override = known.get(override_key)
            if not override or override.kind != "fetchcontent":
                continue
            override_source = override_sources / f"{override.normalized}-src"
            if not override_source.exists():
                fetch(override.url, override_ref, override_source, override.name)
            nested_definitions[f"FETCHCONTENT_SOURCE_DIR_{override.variable}"] = str(override_source)
        nested = configure_trace(source, work / "external-build" / dependency.normalized,
                                 nested_definitions, dependency.cmake_args)
        merge(dependencies, nested)
        index_populated_sources(work, dependencies, source_registry, override_sources)
        external_queue.extend(item for item in nested if item.kind == "external")

        for candidate in nested_dependency_projects(source):
            resolved = candidate.resolve()
            if resolved in configured_paths:
                continue
            configured_paths.add(resolved)
            nested_index += 1
            relative = candidate.relative_to(source)
            print(f"[discovery] Nested dependency project: {dependency.name}/{relative}")
            candidate_definitions = dict(nested_definitions)
            candidate_definitions["FETCHCONTENT_BASE_DIR"] = str(
                work / "nested-deps" / f"candidate-{nested_index}")
            try:
                candidate_dependencies = configure_trace(
                    candidate, work / "nested-build" / f"candidate-{nested_index}",
                    candidate_definitions)
            except RuntimeError as error:
                raise RuntimeError(f"nested dependency project {candidate} failed: {error}") from None
            print(f"[discovery] Nested project {dependency.name}/{relative} found "
                  f"{len(candidate_dependencies)} declaration(s)")
            merge(dependencies, candidate_dependencies)
            index_populated_sources(work, dependencies, source_registry, override_sources)
            external_queue.extend(item for item in candidate_dependencies if item.kind == "external")

    index_populated_sources(work, dependencies, source_registry, override_sources)
    for key, dependency in dependencies.items():
        if dependency.source:
            continue
        ref = overrides.get(key, dependency.ref)
        dependency.source = source_registry.get(key)
        if dependency.source is None:
            dependency.source = work / "declared" / f"{dependency.normalized}-src"
            fetch(dependency.url, ref, dependency.source, dependency.name)
        else:
            print(f"[fetch] {dependency.name}: fetched and checked out by CMake discovery ({ref})")
    return dependencies


def copy_repository(root, stage):
    files = run("git", "ls-files", "-z", cwd=root, capture=True).stdout.split("\0")
    replaced = {"scripts/build_offline_bundle.py", "scripts/compile_offline_module.sh",
                "scripts/syscall_intercept-local-capstone.patch"}
    for relative in files:
        if not relative or relative in replaced:
            continue
        source = root / relative
        if not source.is_file() and not source.is_symlink():
            continue
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink():
            target.symlink_to(os.readlink(source))
        else:
            shutil.copy2(source, target)
    for relative in replaced:
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, target)


def dependency_signature(dependencies, overrides):
    return tuple(sorted((kind, name, dependency.url, overrides.get((kind, name), dependency.ref))
                        for (kind, name), dependency in dependencies.items()))


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Build an offline CAPIO source archive")
    parser.add_argument("output", nargs="?", type=Path, default=root / "dist",
                        help="final archive directory (default: %(default)s)")
    args = parser.parse_args()
    for command in ("cmake", "git"):
        if shutil.which(command) is None:
            raise SystemExit(f"error: {command} is required")

    match = re.search(r"\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                      (root / "CMakeLists.txt").read_text())
    if not match:
        raise SystemExit("error: could not read CAPIO version")
    build_type, tests_choice, logger_choice = select_configuration()
    include_tests = tests_choice == "Tests"
    include_logger = logger_choice == "Logger"
    print("\nConfiguration")
    print(f"  Build:  {build_type}")
    print(f"  Tests:  {'enabled' if include_tests else 'disabled'}")
    print(f"  Logger: {'enabled' if include_logger else 'disabled'}")
    if include_logger and build_type != "Debug":
        print("  Warning: CAPIO logging is selected but only activates in Debug builds.")
    package = f"capio-{match.group(1)}-offline-source"
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir(exist_ok=True)

    phase = "initial dependency discovery"
    try:
        with tempfile.TemporaryDirectory(prefix="capio-offline-source.") as work_name, \
                tempfile.TemporaryDirectory(prefix=".capio-archive.", dir=output.parent) as archive_name:
            work = Path(work_name)
            print("\n[discovery] Initial pass")
            dependencies = discover(root, work / "discovery-0", build_type, include_tests, {}, {})
            overrides = {}
            if prompt_yes_no("Override dependency Git refs?", False):
                prompted = set()
                previous = None
                for iteration in range(1, 6):
                    for key, dependency in sorted(dependencies.items()):
                        if key not in prompted:
                            answer = input(f"  {dependency.name} ref [{dependency.ref}]: ").strip()
                            if answer:
                                overrides[key] = answer
                            prompted.add(key)
                    known = dependencies
                    phase = f"dependency rediscovery pass {iteration}"
                    print(f"[discovery] Rediscovery pass {iteration} of 5")
                    dependencies = discover(root, work / f"discovery-{iteration}", build_type,
                                            include_tests, overrides, known)
                    signature = dependency_signature(dependencies, overrides)
                    if signature == previous and all(key in prompted for key in dependencies):
                        break
                    previous = signature
                else:
                    raise RuntimeError("dependency discovery did not converge after 5 passes")

            print(f"[discovery] {len(dependencies)} unique dependency source(s) ready")
            phase = "staging source tree"
            print("[package] Staging CAPIO source tree")
            stage = work / package
            stage.mkdir()
            copy_repository(root, stage)
            vendor = stage / "vendor"
            lock = []
            print(f"[package] Copying {len(dependencies)} dependency source(s)")
            for key, dependency in sorted(dependencies.items()):
                phase = f"packaging dependency {dependency.name}"
                print(f"  - {dependency.name} ({dependency.kind})")
                destination = (vendor / "_deps" / f"{dependency.normalized}-src"
                               if dependency.kind == "fetchcontent" else vendor / dependency.normalized)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(dependency.source, destination, symlinks=True)
                resolved = run("git", "rev-parse", "HEAD", cwd=destination, capture=True).stdout.strip()
                lock.append(f"{dependency.kind}\t{dependency.name}\t{dependency.url}\t"
                            f"{overrides.get(key, dependency.ref)}\t{resolved}\n")

            phase = "patching vendored dependencies"
            print("[patch] Applying offline source patch")
            patch = stage / "scripts" / "syscall_intercept-local-capstone.patch"
            patched = False
            for dependency in dependencies.values():
                if dependency.kind != "external":
                    continue
                target = vendor / dependency.normalized
                if run("git", "apply", "--check", str(patch), cwd=target,
                       capture=True, check=False).returncode == 0:
                    run("git", "apply", str(patch), cwd=target)
                    patched = True
                    break
            if not patched:
                raise RuntimeError(f"no discovered ExternalProject source accepts {patch.name}")

            for dependency in dependencies.values():
                if dependency.kind == "fetchcontent":
                    alias = vendor / dependency.normalized
                    if not alias.exists():
                        alias.symlink_to(Path("_deps") / f"{dependency.normalized}-src")
            for git_directory in stage.rglob(".git"):
                if git_directory.is_dir():
                    shutil.rmtree(git_directory)
            phase = "writing dependency lock and CMake cache"
            print("[write] Writing dependency lock and offline CMake cache")
            (stage / "offline-dependencies.lock").write_text("".join(lock))
            cache = [f'set(CMAKE_BUILD_TYPE "{build_type}" CACHE STRING "")',
                     f'set(CAPIO_BUILD_TESTS {"ON" if include_tests else "OFF"} CACHE BOOL "")',
                     'set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "")']
            for dependency in dependencies.values():
                if dependency.kind == "fetchcontent":
                    cache.append(f'set(FETCHCONTENT_SOURCE_DIR_{dependency.variable} '
                                 f'"${{CMAKE_CURRENT_LIST_DIR}}/vendor/_deps/{dependency.normalized}-src" '
                                 'CACHE PATH "")')
            cache.append("set(CAPIO_LOG {} CACHE BOOL \"\")".format(
                "ON" if include_logger else "OFF"))
            (stage / "offline-source.cmake").write_text("\n".join(cache) + "\n")
            phase = "archiving offline source bundle"
            print("[archive] Creating compressed archive")
            temporary_archive = Path(archive_name) / f"{package}.tar.gz"
            with tarfile.open(temporary_archive, "w:gz") as archive:
                archive.add(stage, arcname=package)
            final_archive = output / temporary_archive.name
            os.replace(temporary_archive, final_archive)
        print("\nComplete")
        print(f"  Dependencies: {len(dependencies)}")
        print(f"  Output: {final_archive}")
    except (RuntimeError, OSError) as error:
        raise SystemExit(f"error: {phase} failed: {error}") from None


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit("\nCancelled.") from None
