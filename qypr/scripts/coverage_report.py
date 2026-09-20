#!/usr/bin/env python3
import os
import subprocess
import re
import sys

def main():
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    build_dir = os.path.join(root, "build")
    src_dir = os.path.join(root, "src")

    if not os.path.exists(build_dir):
        print("Build directory does not exist. Please build the project first.")
        sys.exit(1)

    # Find all gcda files
    gcda_files = []
    for r, d, files in os.walk(build_dir):
        for f in files:
            if f.endswith(".gcda"):
                gcda_files.append(os.path.join(r, f))

    if not gcda_files:
        print("No .gcda files found. Have the tests/binary been run?")
        sys.exit(1)

    # Clean old .gcov files in root first
    def clean_gcov():
        for f in os.listdir(root):
            if f.endswith(".gcov") or "#" in f:
                try:
                    os.remove(os.path.join(root, f))
                except:
                    pass

    clean_gcov()

    # A map from source file relative path -> dict of {line_num: val}
    # val: None = not executable, -1 = executable but not covered, >0 = covered/execution count
    file_lines = {}

    for gcda in gcda_files:
        obj_dir = os.path.dirname(gcda)
        # Use gcov -p to preserve paths and output to root
        subprocess.run(["gcov", "-p", "-o", obj_dir, gcda], capture_output=True, cwd=root)

        # Parse all generated .gcov files in the root directory
        gcov_files = [f for f in os.listdir(root) if f.endswith(".gcov")]
        for gcov_name in gcov_files:
            gcov_path = os.path.join(root, gcov_name)
            with open(gcov_path, "r", errors="ignore") as f:
                lines = f.readlines()
            if not lines:
                continue

            # The first line or two of .gcov contains the source file name, e.g.:
            # -:    0:Source:/home/arch/.config/qypr/src/core/EventLoop.cpp
            source_line = None
            for line in lines[:5]:
                if "Source:" in line:
                    source_line = line
                    break
            if not source_line:
                continue

            # Extract source path
            src_path = source_line.split("Source:", 1)[1].strip()
            src_path = os.path.abspath(src_path)
            if not src_path.startswith(src_dir):
                continue

            rel_path = os.path.relpath(src_path, root)
            if rel_path not in file_lines:
                file_lines[rel_path] = {}

            # Parse each line in the .gcov file
            for line in lines:
                parts = line.split(":", 2)
                if len(parts) < 3:
                    continue
                exec_count_str = parts[0].strip()
                try:
                    line_num = int(parts[1].strip())
                except ValueError:
                    continue

                if line_num == 0:
                    continue # metadata line

                if exec_count_str == "-":
                    # Non-executable line
                    is_exec = False
                    count = 0
                elif exec_count_str == "#####" or exec_count_str == "$$$$$":
                    # Executable but not executed
                    is_exec = True
                    count = 0
                else:
                    # Executable and executed
                    is_exec = True
                    try:
                        count = int(exec_count_str.split("*")[0]) # handle gcov * markers
                    except ValueError:
                        count = 1

                if is_exec:
                    current = file_lines[rel_path].get(line_num, None)
                    if count > 0:
                        # Executed: this always overrides or updates
                        if current is None or current == -1:
                            file_lines[rel_path][line_num] = count
                        else:
                            file_lines[rel_path][line_num] = current + count
                    else:
                        # Executable but not executed: only set if we don't have execution data yet
                        if current is None:
                            file_lines[rel_path][line_num] = -1

            # Delete the gcov file immediately to avoid double processing or pollution
            try:
                os.remove(gcov_path)
            except:
                pass

    # Find all actual source files in src/ to make sure we don't omit files with 0% coverage (not compile/linked in tests)
    all_src_files = []
    for r, d, files in os.walk(src_dir):
        for f in files:
            if f.endswith(".cpp") or f.endswith(".hpp") or f.endswith(".h"):
                rel = os.path.relpath(os.path.join(r, f), root)
                all_src_files.append(rel)

    # For files that had no .gcda/.gcov info, populate them with 0% coverage
    for f in all_src_files:
        if f not in file_lines:
            file_lines[f] = {}

    # Print results
    print(f"{'File':<50} | {'Covered':<8} | {'Total':<8} | {'Coverage':<8}")
    print("-" * 83)

    total_executed = 0
    total_executable = 0

    sorted_files = sorted(file_lines.keys())
    for f in sorted_files:
        lines_info = file_lines[f]
        executable = 0
        covered = 0
        for lnum, val in lines_info.items():
            if val is not None: # executable
                executable += 1
                if val > 0:
                    covered += 1

        total_executed += covered
        total_executable += executable

        pct_str = "N/A"
        if executable > 0:
            pct = (covered / executable) * 100.0
            pct_str = f"{pct:.2f}%"
        elif f.endswith(".hpp") or f.endswith(".h"):
            # Header files without executable lines
            continue

        print(f"{f:<50} | {covered:<8} | {executable:<8} | {pct_str:<8}")

    print("-" * 83)
    overall_pct = 0.0
    if total_executable > 0:
        overall_pct = (total_executed / total_executable) * 100.0
    print(f"{'OVERALL':<50} | {total_executed:<8} | {total_executable:<8} | {overall_pct:.2f}%")

    if overall_pct >= 100.0:
        print("SUCCESS: 100% test coverage achieved!")
        sys.exit(0)
    else:
        print(f"FAILED: Coverage is {overall_pct:.2f}% (needs 100.00%)")
        sys.exit(1)

if __name__ == "__main__":
    main()
