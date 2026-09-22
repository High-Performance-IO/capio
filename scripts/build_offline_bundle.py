#!/usr/bin/env python3
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
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
    def key(self):
        return re.sub(r"[^a-z0-9_.-]+", "_", self.name.lower())

    @property
    def variable(self):
        return re.sub(r"[^A-Z0-9_]+", "_", self.name.upper())


def prompt_choice(prompt, choices, default, expected):
    while True:
        answer = input(prompt).strip().lower()
        if not answer:
            return default
        for value, aliases in choices.items():
            if answer in aliases:
                return value
        print(f"Invalid input. Enter {expected}.")


def select_configuration():
    build, tests, logger = "Release", False, False
    if not sys.stdin.isatty():
        print("Non-interactive input: using Release, no tests, no logger.")
        return build, tests, logger
    try:
        build = prompt_choice("Build type [Release/Debug] (Release): ",
                              {"Release": ("release", "r"), "Debug": ("debug", "d")},
                              "Release", "release/r or debug/d")
        tests = prompt_choice("Include tests? [y/N]: ",
                              {True: ("y", "yes"), False: ("n", "no")}, False, "y or n")
        logger = prompt_choice("Enable CAPIO logger? [y/N]: ",
                               {True: ("y", "yes"), False: ("n", "no")}, False, "y or n")
    except EOFError:
        print("\nEOF: using defaults for unanswered prompts.")
    return build, tests, logger


def yes_no(prompt):
    if not sys.stdin.isatty():
        return False
    try:
        return prompt_choice(f"{prompt} [y/N]: ",
                             {True: ("y", "yes"), False: ("n", "no")}, False, "y or n")
    except EOFError:
        print("\nEOF: defaulting to no.")
        return False


def run(*command, cwd=None, capture=False, check=True):
    result = subprocess.run(command, cwd=cwd, check=False, text=True,
                            stdout=subprocess.PIPE if capture else None,
                            stderr=subprocess.PIPE if capture else None)
    if check and result.returncode:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"command failed ({' '.join(map(str, command))}): {detail}")
    return result


def fetch(dependency, destination, ref):
    print(f"[fetch] {dependency.name}: {ref}")
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        run("git", "init", "-q", str(destination))
        run("git", "remote", "add", "origin", dependency.url, cwd=destination)
        shallow = run("git", "fetch", "-q", "--depth", "1", "origin", ref,
                      cwd=destination, capture=True, check=False)
        commit = "FETCH_HEAD"
        if shallow.returncode:
            deepen = ["--unshallow"] if (destination / ".git/shallow").exists() else []
            run("git", "fetch", "-q", *deepen, "origin",
                "+refs/heads/*:refs/remotes/origin/*", "+refs/tags/*:refs/tags/*", cwd=destination)
            commit = next((candidate for candidate in (ref, f"origin/{ref}")
                           if run("git", "rev-parse", "--verify", f"{candidate}^{{commit}}",
                                  cwd=destination, capture=True, check=False).returncode == 0), None)
            if commit is None:
                raise RuntimeError(f"ref {ref!r} could not be resolved")
        run("git", "checkout", "-q", "--detach", commit, cwd=destination)
    except (RuntimeError, OSError) as error:
        raise RuntimeError(f"fetch/check-out failed for {dependency.name}: {error}") from None


def declaration(record, kind):
    args = [part for argument in record.get("args", []) for part in argument.split(";")]
    if not args:
        return None
    values = {value.upper(): args[index + 1] for index, value in enumerate(args[:-1])
              if value.upper() in ("GIT_REPOSITORY", "GIT_TAG")}
    if "GIT_REPOSITORY" not in values:
        raise RuntimeError(f"{kind} dependency {args[0]!r} uses unsupported non-Git metadata")
    cmake_args = ()
    if kind == "external" and "CMAKE_ARGS" in args:
        cmake_args = tuple(value for value in args[args.index("CMAKE_ARGS") + 1:]
                           if value.startswith("-D"))
    return Dependency(kind, args[0], values["GIT_REPOSITORY"], values.get("GIT_TAG", "HEAD"),
                      cmake_args=cmake_args)


def configure_trace(source, build, definitions, cmake_args=()):
    build.parent.mkdir(parents=True, exist_ok=True)
    trace = build.with_name(f"{build.name}-trace.json")
    command = ["cmake", "-Wno-dev", "-Wno-deprecated", "--trace-expand",
               "--trace-format=json-v1", f"--trace-redirect={trace}",
               "-S", str(source), "-B", str(build), "-DCMAKE_POLICY_VERSION_MINIMUM=3.5"]
    command += [f"-D{key}={value}" for key, value in definitions.items()]
    command += [arg.replace("<INSTALL_DIR>", str(build.parent / "install")) for arg in cmake_args]
    print(f"[discovery] Configuring {source}")
    result = run(*command, capture=True, check=False)
    if not trace.is_file():
        raise RuntimeError("CMake JSON tracing is unavailable; --trace-format=json-v1 is required")
    found = []
    for line in trace.read_text().splitlines():
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        command_name = record.get("cmd", "").lower()
        if command_name == "fetchcontent_declare":
            found.append(declaration(record, "fetchcontent"))
        elif command_name == "externalproject_add" and "/Modules/" not in record.get("file", ""):
            found.append(declaration(record, "external"))
    if result.returncode:
        detail = (result.stderr or result.stdout or "").strip()
        raise RuntimeError(f"CMake dependency discovery failed for {source}: {detail}")
    base = Path(definitions["FETCHCONTENT_BASE_DIR"])
    for dependency in found:
        if dependency.kind == "fetchcontent":
            candidate = Path(definitions.get(f"FETCHCONTENT_SOURCE_DIR_{dependency.variable}",
                                             base / f"{dependency.key}-src"))
            if (candidate / ".git").exists():
                dependency.source = candidate
    print(f"[discovery] {len(found)} declaration(s) in {source}")
    for dependency in found:
        print(f"  - {dependency.name}: {dependency.kind}, {dependency.url}, {dependency.ref}")
    return found


def merge(dependencies, found):
    for dependency in found:
        previous = dependencies.get(dependency.key)
        if previous and (previous.kind, previous.url, previous.ref) != (
                dependency.kind, dependency.url, dependency.ref):
            raise RuntimeError(f"conflicting declarations for {dependency.name}")
        if previous:
            previous.source = previous.source or dependency.source
        else:
            dependencies[dependency.key] = dependency


def nested_projects(source):
    excluded = {".git", "_deps", "cmakefiles", "build", "_build", "generated"}
    for directory, names, files in os.walk(source):
        names[:] = [name for name in names if name.lower() not in excluded
                    and not name.lower().startswith(("build-", "cmake-build-"))]
        path = Path(directory)
        if path == source or "CMakeLists.txt" not in files:
            continue
        text = (path / "CMakeLists.txt").read_text(errors="replace")
        if (re.search(r"\bproject\s*\(", text, re.I)
                and re.search(r"\b(?:FetchContent_Declare|ExternalProject_Add)\s*\(", text, re.I)):
            yield path


def discover(root, work, build_type, tests, overrides, known):
    dependencies = {}
    override_dir = work / "overrides"
    definitions = {
        "CAPIO_DEPENDENCY_DISCOVERY": "ON", "CAPIO_BUILD_TESTS": "ON" if tests else "OFF",
        "CMAKE_BUILD_TYPE": build_type, "FETCHCONTENT_BASE_DIR": work / "root-deps",
        "CALF_TESTS": "OFF", "CALF_PYTHON_TESTS": "OFF", "CALF_BUILD_PYTHON_BINDINGS": "OFF",
        "CALF_PROTOBUF_FORCE_FETCH": "ON", "protobuf_BUILD_TESTS": "OFF",
        "protobuf_FORCE_FETCH_DEPENDENCIES": "ON", "ABSL_BUILD_TESTING": "OFF",
    }
    for key, ref in overrides.items():
        dependency = known.get(key)
        if not dependency or dependency.kind != "fetchcontent":
            continue
        source = override_dir / f"{key}-src"
        fetch(dependency, source, ref)
        definitions[f"FETCHCONTENT_SOURCE_DIR_{dependency.variable}"] = source

    found = configure_trace(root, work / "root-build", definitions)
    merge(dependencies, found)
    queue = [dependency.key for dependency in found if dependency.kind == "external"]
    configured, configured_paths = set(), set()
    nested_index = 0
    while queue:
        key = queue.pop(0)
        if key in configured:
            continue
        configured.add(key)
        dependency = dependencies[key]
        source = work / "external" / key
        fetch(dependency, source, overrides.get(key, dependency.ref))
        dependency.source = source
        configured_paths.add(source.resolve())
        nested_definitions = {"CMAKE_BUILD_TYPE": build_type,
                              "FETCHCONTENT_BASE_DIR": work / "external-deps" / key}
        for override_key, ref in overrides.items():
            override = known.get(override_key)
            if not override or override.kind != "fetchcontent":
                continue
            override_source = override_dir / f"{override_key}-src"
            if not override_source.exists():
                fetch(override, override_source, ref)
            nested_definitions[f"FETCHCONTENT_SOURCE_DIR_{override.variable}"] = override_source
        nested = configure_trace(source, work / "external-build" / key,
                                 nested_definitions, dependency.cmake_args)
        merge(dependencies, nested)
        queue += [item.key for item in nested if item.kind == "external"]
        for candidate in nested_projects(source):
            if candidate.resolve() in configured_paths:
                continue
            configured_paths.add(candidate.resolve())
            nested_index += 1
            print(f"[discovery] Nested project {dependency.name}/{candidate.relative_to(source)}")
            candidate_definitions = dict(nested_definitions)
            candidate_definitions["FETCHCONTENT_BASE_DIR"] = work / "nested-deps" / str(nested_index)
            try:
                nested = configure_trace(candidate, work / "nested-build" / str(nested_index),
                                         candidate_definitions)
            except RuntimeError as error:
                raise RuntimeError(f"nested project {candidate} failed: {error}") from None
            merge(dependencies, nested)
            queue += [item.key for item in nested if item.kind == "external"]

    for dependency in dependencies.values():
        if dependency.source:
            continue
        override = override_dir / f"{dependency.key}-src"
        matches = [override] if (override / ".git").exists() else [
            path for path in work.rglob(f"{dependency.key}-src") if (path / ".git").exists()]
        if matches:
            dependency.source = matches[0]
            print(f"[fetch] {dependency.name}: populated by CMake ({overrides.get(dependency.key, dependency.ref)})")
        else:
            dependency.source = work / "declared" / f"{dependency.key}-src"
            fetch(dependency, dependency.source, overrides.get(dependency.key, dependency.ref))
    return dependencies


def copy_repository(root, stage):
    overlays = {"scripts/build_offline_bundle.py", "scripts/compile_offline_module.sh"}
    files = set(run("git", "ls-files", "-z", cwd=root, capture=True).stdout.split("\0")) | overlays
    for relative in files:
        source = root / relative
        if not relative or (not source.is_file() and not source.is_symlink()):
            continue
        target = stage / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.is_symlink():
            target.symlink_to(os.readlink(source))
        else:
            shutil.copy2(source, target)


def main():
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Build an offline CAPIO source archive")
    parser.add_argument("output", nargs="?", type=Path, default=root / "dist")
    args = parser.parse_args()
    for command in ("cmake", "git"):
        if shutil.which(command) is None:
            raise SystemExit(f"error: {command} is required")
    version = re.search(r"\bVERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                        (root / "CMakeLists.txt").read_text())
    if not version:
        raise SystemExit("error: could not read CAPIO version")
    build_type, tests, logger = select_configuration()
    print(f"\nConfiguration\n  Build:  {build_type}\n  Tests:  {'enabled' if tests else 'disabled'}"
          f"\n  Logger: {'enabled' if logger else 'disabled'}")
    if logger and build_type != "Debug":
        print("  Warning: CAPIO logging only activates in Debug builds.")
    package = f"capio-{version.group(1)}-offline-source"
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    phase = "dependency discovery"
    try:
        with tempfile.TemporaryDirectory(prefix="capio-offline-source.") as temporary, \
                tempfile.TemporaryDirectory(prefix=".capio-archive.", dir=output.parent) as archive_dir:
            work = Path(temporary)
            dependencies = discover(root, work / "discovery-0", build_type, tests, {}, {})
            overrides, prompted, pass_number = {}, set(), 0
            if yes_no("Override dependency Git refs?"):
                while True:
                    entered = False
                    for key, dependency in sorted(dependencies.items()):
                        if key in prompted:
                            continue
                        answer = input(f"  {dependency.name} ref [{dependency.ref}]: ").strip()
                        prompted.add(key)
                        if answer:
                            overrides[key], entered = answer, True
                    if not entered:
                        break
                    pass_number += 1
                    dependencies = discover(root, work / f"discovery-{pass_number}", build_type,
                                            tests, overrides, dependencies)

            print(f"[package] Staging {len(dependencies)} dependency source(s)")
            phase = "source staging"
            stage = work / package
            stage.mkdir()
            copy_repository(root, stage)
            vendor, lock = stage / "vendor", []
            for key, dependency in sorted(dependencies.items()):
                destination = (vendor / "_deps" / f"{key}-src"
                               if dependency.kind == "fetchcontent" else vendor / key)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(dependency.source, destination, symlinks=True)
                head = run("git", "rev-parse", "HEAD", cwd=destination, capture=True).stdout.strip()
                lock.append(f"{dependency.kind}\t{dependency.name}\t{dependency.url}\t"
                            f"{overrides.get(key, dependency.ref)}\t{head}\n")

            phase = "vendored dependency patching"
            for patch in (stage / "scripts").glob("*.patch"):
                for dependency in dependencies.values():
                    target = vendor / dependency.key
                    if dependency.kind == "external" and run(
                            "git", "apply", "--check", str(patch), cwd=target,
                            capture=True, check=False).returncode == 0:
                        run("git", "apply", str(patch), cwd=target)
                        break
                else:
                    raise RuntimeError(f"no discovered ExternalProject source accepts {patch.name}")
            for dependency in dependencies.values():
                if dependency.kind == "fetchcontent" and not (vendor / dependency.key).exists():
                    (vendor / dependency.key).symlink_to(Path("_deps") / f"{dependency.key}-src")
            for git_directory in stage.rglob(".git"):
                if git_directory.is_dir():
                    shutil.rmtree(git_directory)

            (stage / "offline-dependencies.lock").write_text("".join(lock))
            cache = [f'set(CMAKE_BUILD_TYPE "{build_type}" CACHE STRING "")',
                     f'set(CAPIO_BUILD_TESTS {"ON" if tests else "OFF"} CACHE BOOL "")',
                     f'set(CAPIO_LOG {"ON" if logger else "OFF"} CACHE BOOL "")',
                     'set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "")']
            cache += [f'set(FETCHCONTENT_SOURCE_DIR_{dependency.variable} '
                      f'"${{CMAKE_CURRENT_LIST_DIR}}/vendor/_deps/{dependency.key}-src" CACHE PATH "")'
                      for dependency in dependencies.values() if dependency.kind == "fetchcontent"]
            (stage / "offline-source.cmake").write_text("\n".join(cache) + "\n")

            phase = "archive creation"
            archive = Path(archive_dir) / f"{package}.tar.gz"
            print(f"[archive] Creating {archive.name}")
            with tarfile.open(archive, "w:gz") as output_archive:
                output_archive.add(stage, arcname=package)
            final_archive = output / archive.name
            os.replace(archive, final_archive)
        print(f"\nComplete\n  Dependencies: {len(dependencies)}\n  Output: {final_archive}")
    except (RuntimeError, OSError) as error:
        raise SystemExit(f"error: {phase} failed: {error}") from None


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        raise SystemExit("\nCancelled.") from None
