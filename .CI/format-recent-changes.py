from datetime import datetime, timezone
import os
import subprocess
import re
import argparse


ROOT_DOWNLOAD_URL = (
    "https://github.com/SevenTV/chatterino7/releases/download/nightly-build"
)
WIN_X64_INSTALLERS = ["Chatterino7TV.Nightly.Installer.exe"]
WIN_ARM64_INSTALLERS = ["Experimental-ARM64-Chatterino7TV.Nightly.Installer.exe"]


def create_artifacts_table(artifact_dir: str, include_installer: bool):
    artifacts = os.listdir(artifact_dir)

    def get(r: str):
        return [n for n in artifacts if re.search(r, n, re.IGNORECASE)]

    model = {
        "Windows": {
            "x86_64": get(r"-windows-.*x86.*\.zip$")
            + (WIN_X64_INSTALLERS if include_installer else []),
            "ARM64<br>(Experimental)": get(r"arm64.*-windows-.*\.zip$")
            + (WIN_ARM64_INSTALLERS if include_installer else []),
        },
        "macOS": {
            "Universal (x86_64, ARM64)": get(r"\.dmg$"),
        },
        "Linux": {"x86_64": get(r"\.flatpak")},
    }

    # Cleanup
    s = """<table align="center">
        <thead>
            <tr>
            <th scope="col">OS</th>
            <th scope="col">Arch</th>
            <th scope="col">File</th>
            </tr>
        </thead>
        <tbody>
    """
    for os_name, archs in model.items():
        for io, (arch, files) in enumerate(archs.items()):
            for ia, file in enumerate(files):
                s += "<tr>"
                if io == 0 and ia == 0:
                    s += f'<td rowspan="{sum(len(f) for f in archs.values())}" colspan="1">{os_name}</td>'
                if ia == 0:
                    s += f'<td rowspan="{len(files)}" colspan="1">{arch}</td>'
                s += f'<td><a href="{ROOT_DOWNLOAD_URL}/{file}">{file}</a></td>'
                s += "</tr>"
    s += "</tbody></table>"
    return s


def run_git_command(args: list[str]) -> str:
    p = subprocess.run(
        ["git", *args],
        cwd=os.path.dirname(os.path.realpath(__file__)),
        text=True,
        check=True,
        capture_output=True,
    )
    return p.stdout.strip()


def get_last_version_tag() -> str | None:
    try:
        return run_git_command(["describe", "--tags", "--abbrev=0", "--match", "v2*"])
    except subprocess.CalledProcessError:
        return None


def get_unreleased_commits():
    last_tag = get_last_version_tag()
    if last_tag:
        log_range = f"{last_tag}..HEAD"
        limit = None
    else:
        # no version tag -> just take a few recent commits
        log_range = "HEAD"
        limit = 10
    args = [
        "log",
        log_range,
        "--pretty=format:%cI|%an|%s",
        "--no-merges",
    ]
    if limit:
        args.insert(1, f"-n{limit}")
    log_output = run_git_command(args)
    unreleased: list[tuple[datetime, str]] = []
    for line in log_output.splitlines():
        if not line.strip():
            continue
        date_str, author, subject = line.split("|", 2)
        if author.lower() == "dependabot[bot]":
            continue
        if subject.startswith("Merge "):
            continue
        d = datetime.fromisoformat(date_str).astimezone(timezone.utc)
        content = f"- [{d.strftime('%Y-%m-%d')}] {subject}"
        unreleased.append((d, content))
    unreleased.sort(key=lambda it: it[0], reverse=True)
    return unreleased


def get_current_stable():
    p = subprocess.run(
        ["git", "describe", "--tags", "--abbrev=0", "--match", "v2.*.[0-9]"],
        cwd=os.path.dirname(os.path.realpath(__file__)),
        text=True,
        check=True,
        capture_output=True,
    )
    return p.stdout.strip()


unreleased_lines = get_unreleased_commits()

parser = argparse.ArgumentParser()
parser.add_argument("--artifacts", required=True)
parser.add_argument("--include-installer", action="store_true")
args = parser.parse_args()


print("> [!WARNING]")
print(
    "> This is an experimental version that may break. "
    "If you're looking for the latest stable release, see "
    f"https://github.com/SevenTV/chatterino7/releases/tag/{get_current_stable()}.\n"
)

print("### Downloads\n")

print(create_artifacts_table(args.artifacts, args.include_installer))
print("\n### What's Changed\n")

if len(unreleased_lines) == 0:
    print("No changes since last release.")

for _, line in unreleased_lines[:5]:
    print(line)

if len(unreleased_lines) > 5:
    print("<details><summary>More Changes</summary>\n")
    for _, line in unreleased_lines[5:]:
        print(line)
    print("</details>")
