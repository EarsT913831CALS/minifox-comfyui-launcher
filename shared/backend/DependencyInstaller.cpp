#include "DependencyInstaller.h"

#include <QFileInfo>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace DependencyInstaller {

QString checkerScript()
{
    static const QString checker = QStringLiteral(R"PY(
import importlib.metadata as metadata
import json
import os
import re
import sys
from urllib.parse import unquote, urlparse
from urllib.request import url2pathname

try:
    from packaging.requirements import Requirement
    from packaging.version import Version
except Exception:
    try:
        from pip._vendor.packaging.requirements import Requirement
        from pip._vendor.packaging.version import Version
    except Exception as exc:
        print(json.dumps({"ok": False, "fatal": "Python 中缺少 pip/packaging，无法解析依赖清单：%s" % exc}))
        raise SystemExit(3)

ARCHIVE_SUFFIXES = (".whl", ".zip", ".tar.gz", ".tar.bz2", ".tgz", ".tar")

def canonical(name):
    return re.sub(r"[-_.]+", "-", name).lower()

def option_value(line, short, long):
    # pip accepts "-r x", "-rx", "--requirement x" and "--requirement=x".
    match = re.match(r"^%s\s*=?\s*(\S.*)$" % re.escape(short), line)
    if match and not line.startswith("--"):
        return match.group(1).strip().strip("\"'")
    match = re.match(r"^%s(?:\s*=\s*|\s+)(\S.*)$" % re.escape(long), line)
    if match:
        return match.group(1).strip().strip("\"'")
    return None

def strip_inline_options(line):
    # "pkg==1 --hash=sha256:..." and similar per-requirement options.
    return re.split(r"\s+(?=--?[A-Za-z])", line, maxsplit=1)[0].strip()

def expand_environment(line):
    return re.sub(r"\$\{([A-Za-z0-9_]+)\}", lambda m: os.environ.get(m.group(1), m.group(0)), line)

def normalized_vcs_url(value):
    value = value.strip()
    if re.match(r"^(?:git|hg|svn|bzr)\+", value, re.IGNORECASE):
        value = value.split("+", 1)[1]
    value = value.split("#", 1)[0].rstrip("/")
    git_revision = value.lower().find(".git@")
    if git_revision >= 0:
        value = value[:git_revision + 4]
    else:
        last_slash = value.rfind("/")
        revision = value.rfind("@")
        if revision > last_slash:
            value = value[:revision]
    if value.lower().endswith(".git"):
        value = value[:-4]
    return value.rstrip("/").casefold()

def normalized_location(value, base):
    value = value.split("#", 1)[0].strip()
    if re.match(r"^file:", value, re.IGNORECASE):
        parsed = urlparse(value)
        path = url2pathname(unquote((parsed.netloc and "//" + parsed.netloc or "") + parsed.path))
        return os.path.normcase(os.path.abspath(path))
    if re.match(r"^[a-z][a-z0-9+.-]*://", value, re.IGNORECASE):
        return value.rstrip("/").casefold()
    return os.path.normcase(os.path.abspath(os.path.join(base, value)))

def installed_direct_distribution(matches):
    for distribution in metadata.distributions():
        try:
            direct_url_text = distribution.read_text("direct_url.json")
            if not direct_url_text:
                continue
            if matches(json.loads(direct_url_text).get("url", "")):
                name = distribution.metadata.get("Name") or distribution.name
                return name, distribution.version
        except Exception:
            continue
    return None

def is_location(raw):
    if " @ " in raw:
        return False
    target = raw.split(";", 1)[0].strip()
    return ("/" in target or "\\" in target or target.startswith(".")
            or target.lower().endswith(ARCHIVE_SUFFIXES)
            or re.match(r"^[a-z][a-z0-9+.-]*://", target, re.IGNORECASE) is not None)

def check_file(root):
    root = os.path.abspath(root)
    root_dir = os.path.dirname(root)
    seen_files = set()
    requirements = []
    locations = []
    constraints = []
    parse_errors = []

    def read_file(path, constraint_mode):
        path = os.path.abspath(path)
        key = (path, constraint_mode)
        if key in seen_files:
            return
        seen_files.add(key)
        try:
            with open(path, "r", encoding="utf-8-sig") as stream:
                physical = stream.readlines()
        except Exception as exc:
            parse_errors.append("无法读取 %s：%s" % (path, exc))
            return

        logical = []
        pending = ""
        for raw in physical:
            line = raw.rstrip("\r\n")
            if line.rstrip().endswith("\\"):
                pending += line.rstrip()[:-1] + " "
                continue
            logical.append(pending + line)
            pending = ""
        if pending:
            logical.append(pending)

        base = os.path.dirname(path)
        for line in logical:
            line = re.split(r"(?:^|\s+)#", line, maxsplit=1)[0].strip()
            if not line:
                continue
            line = expand_environment(line)
            include = option_value(line, "-r", "--requirement")
            if include is not None:
                read_file(os.path.join(base, include), constraint_mode)
                continue
            constraint = option_value(line, "-c", "--constraint")
            if constraint is not None:
                read_file(os.path.join(base, constraint), True)
                continue
            editable = option_value(line, "-e", "--editable")
            if editable is not None:
                if constraint_mode:
                    continue
                editable = strip_inline_options(editable)
                egg = re.search(r"[#&]egg=([^&]+)", editable)
                if egg:
                    requirements.append(egg.group(1))
                else:
                    locations.append(editable)
                continue
            if line.startswith("-"):
                # Global pip options such as --index-url, -f, --pre or --no-binary.
                continue
            line = strip_inline_options(line)
            if constraint_mode:
                constraints.append(line)
            elif is_location(line):
                locations.append(line)
            else:
                requirements.append(line)

    read_file(root, False)

    constraint_map = {}
    for raw in constraints:
        try:
            item = Requirement(raw)
            if item.marker is not None and not item.marker.evaluate():
                continue
            constraint_map.setdefault(canonical(item.name), []).append(item)
        except Exception:
            # pip only applies constraints to packages that are actually required;
            # an unparsable or unused constraint must not block anything here.
            continue

    issues = []
    checked = 0

    for raw in locations:
        target = raw.split(";", 1)[0].strip()
        checked += 1
        if re.match(r"^(?:git|hg|svn|bzr)\+\S+$", target, re.IGNORECASE):
            expected = normalized_vcs_url(target)
            if installed_direct_distribution(lambda url: normalized_vcs_url(url) == expected) is None:
                repository = expected.rsplit("/", 1)[-1]
                issues.append("缺少 VCS 依赖：%s（来源：%s）" % (repository, target))
            continue
        filename = re.split(r"[/\\]", target.split("#", 1)[0].rstrip("/\\"))[-1]
        if filename.lower().endswith(".whl"):
            parts = filename[:-4].split("-")
            if len(parts) >= 2:
                name, wanted = parts[0], parts[1]
                try:
                    installed = metadata.version(name)
                except metadata.PackageNotFoundError:
                    issues.append("缺少依赖：%s（来源：%s）" % (name, target))
                    continue
                try:
                    matches = Version(installed) == Version(wanted)
                except Exception:
                    matches = installed == wanted
                if not matches:
                    issues.append("版本不符：%s 已安装 %s，需要 %s" % (name, installed, wanted))
                continue
        expected = normalized_location(target, root_dir)
        found = installed_direct_distribution(
            lambda url: bool(url) and normalized_location(url, root_dir) == expected)
        if found is None:
            issues.append("缺少本地或直链依赖：%s" % target)

    for raw in requirements:
        try:
            requirement = Requirement(raw)
        except Exception as exc:
            parse_errors.append("无法解析依赖“%s”：%s" % (raw, exc))
            continue
        try:
            if requirement.marker is not None and not requirement.marker.evaluate():
                continue
        except Exception as exc:
            parse_errors.append("无法计算依赖条件“%s”：%s" % (raw, exc))
            continue
        checked += 1
        try:
            installed = metadata.version(requirement.name)
        except metadata.PackageNotFoundError:
            issues.append("缺少依赖：%s" % requirement.name)
            continue
        except Exception as exc:
            issues.append("无法读取 %s 的版本：%s" % (requirement.name, exc))
            continue
        if requirement.specifier and not requirement.specifier.contains(installed, prereleases=True):
            issues.append("版本不符：%s 已安装 %s，需要 %s" %
                          (requirement.name, installed, requirement.specifier))
            continue
        for constraint in constraint_map.get(canonical(requirement.name), []):
            if constraint.specifier and not constraint.specifier.contains(installed, prereleases=True):
                issues.append("版本不符合约束：%s 已安装 %s，约束 %s" %
                              (requirement.name, installed, constraint.specifier))
                break

    return {
        "path": root,
        "ok": not issues and not parse_errors,
        "checked": checked,
        "issues": issues,
        "parseErrors": parse_errors,
    }

results = [check_file(path) for path in sys.argv[1:]]
ok = all(item["ok"] for item in results)
print(json.dumps({"ok": ok, "results": results}))
raise SystemExit(0 if ok else 2)
)PY");
    return checker;
}

QString installerScript()
{
    static const QString installer = QStringLiteral(R"PY(
import codecs
import os
import subprocess
import sys
import time

def console_writer():
    if os.name != "nt":
        return None
    try:
        import ctypes
        from ctypes import wintypes
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel32.CreateFileW.restype = wintypes.HANDLE
        handle = kernel32.CreateFileW("CONOUT$", 0xC0000000, 3, None, 3, 0, None)
        if not handle or handle == wintypes.HANDLE(-1).value:
            return None
        kernel32.SetConsoleTitleW("Minifox - pip install")
        written = wintypes.DWORD()
        def write(text):
            while text:
                chunk, text = text[:4096], text[4096:]
                units = len(chunk.encode("utf-16-le")) // 2
                kernel32.WriteConsoleW(handle, chunk, units, ctypes.byref(written), None)
        return write
    except Exception:
        return None

requirements = sys.argv[1]
console = console_writer()
show = console or (lambda text: None)
show("Minifox 正在安装依赖\n清单：%s\n目录：%s\n\n" % (requirements, os.getcwd()))

environment = dict(os.environ)
environment["PYTHONUNBUFFERED"] = "1"
environment["PYTHONIOENCODING"] = "utf-8"
command = [sys.executable, "-m", "pip", "install", "-r", requirements]
try:
    process = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               stdin=subprocess.DEVNULL, env=environment)
except OSError as exc:
    message = "无法启动 pip：%s\n" % exc
    sys.stdout.write(message)
    sys.stdout.flush()
    show(message)
    code = 1
else:
    decoder = codecs.getincrementaldecoder("utf-8")("replace")
    while True:
        chunk = process.stdout.read1(4096)
        if not chunk:
            break
        sys.stdout.buffer.write(chunk)
        sys.stdout.buffer.flush()
        show(decoder.decode(chunk))
    show(decoder.decode(b"", final=True))
    code = process.wait()

if console:
    if code == 0:
        show("\n依赖安装完成，窗口即将关闭。\n")
        time.sleep(1)
    else:
        try:
            pause = max(0, int(os.environ.get("MINIFOX_DEPENDENCY_FAILURE_PAUSE", "30")))
        except ValueError:
            pause = 30
        show("\n依赖安装失败（退出码 %s）。按任意键关闭此窗口，或等待 %s 秒自动关闭。\n" % (code, pause))
        try:
            import msvcrt
            deadline = time.monotonic() + pause
            while time.monotonic() < deadline and not msvcrt.kbhit():
                time.sleep(0.1)
        except Exception:
            pass
sys.exit(code)
)PY");
    return installer;
}

void configureCheck(QProcess &process, const QString &python, const QStringList &requirements)
{
    QStringList arguments{QStringLiteral("-c"), checkerScript()};
    arguments.append(requirements);
    process.setProgram(python);
    process.setArguments(arguments);
#ifdef Q_OS_WIN
    process.setNativeArguments({});
    process.setCreateProcessArgumentsModifier({});
#endif
}

void configure(QProcess &process, const QString &python, const QString &requirements)
{
    process.setProgram(python);
    process.setArguments({QStringLiteral("-c"), installerScript(), requirements});
    process.setWorkingDirectory(QFileInfo(requirements).absolutePath());
#ifdef Q_OS_WIN
    process.setNativeArguments({});
    // Package changes must be visible: open a console for pip while keeping the
    // standard handles piped, so the launcher still logs output and tracks the exit code.
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags &= ~CREATE_NO_WINDOW;
        arguments->flags |= CREATE_NEW_CONSOLE;
    });
#endif
}

} // namespace DependencyInstaller
