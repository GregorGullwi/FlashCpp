"""Check supported/deferred production requests and capture structural traces."""
import argparse
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[2]
BASELINE = ROOT / "tests/migration_counters/canonical_adapter_baseline.tsv"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", required=True)
    options = parser.parse_args()
    compiler = pathlib.Path(options.compiler).resolve()
    output = ROOT / "x64/canonical-adapter-traces"
    output.mkdir(parents=True, exist_ok=True)
    for row in BASELINE.read_text().splitlines():
        if not row or row.startswith("#"):
            continue
        source, supported, deferred = row.split("\t")
        name = pathlib.Path(source).stem
        result = subprocess.run([str(compiler), source, "-o", str(output / (name + ".obj")),
                                 "--perf-stats", "--log-level=Types:trace"],
                                cwd=ROOT, capture_output=True, text=True)
        log = re.sub(r"\x1b\[[0-9;]*m", "", result.stdout + result.stderr)
        (output / (name + ".log")).write_text(log)
        if result.returncode != 0:
            raise RuntimeError(f"{source}: compiler exited {result.returncode}")
        counts = []
        for label in ("Canonical", "Unmigrated"):
            match = re.search(label + r" declarator requests: (\d+)", log)
            if not match:
                raise RuntimeError(f"{source}: missing {label} counter")
            counts.append(int(match.group(1)))
        if counts[0] < int(supported) or counts[1] > int(deferred):
            raise RuntimeError(f"{source}: supported/deferred {counts}, baseline {supported}/{deferred}")
        traces = re.findall(r"canonical-request-v1 ([^\r\n]+)", log)
        if not traces:
            raise RuntimeError(f"{source}: no production structural trace")
        for trace in traces:
            if not re.fullmatch(r"(?:[1-4],0,[0-3]/)*0,\d+,0", trace):
                raise RuntimeError(f"{source}: malformed structural request {trace}")
        (output / (name + ".trace")).write_text("\n".join(traces) + "\n")
        print(f"{source}: supported={counts[0]} deferred={counts[1]} traces={len(traces)}")


if __name__ == "__main__":
    main()
