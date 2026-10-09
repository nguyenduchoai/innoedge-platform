# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Cài bản phát hành (app, bundle mô hình, nội dung) lên thiết bị Linux — OTA cho
mọi thứ không phải firmware.

    root/
      releases/<version>/   mỗi bản một thư mục, không sửa sau khi cài
      current -> releases/<version>   symlink đổi NGUYÊN TỬ (os.replace)
      previous -> releases/<cũ>       để rollback một lệnh

Luật giống OTA của ESP32: chỉ https, kiểm đúng số byte + SHA-256 trước khi đụng
tới `current`, có cửa kiểm `validate` do ứng dụng cấp (vd chạy thử --dry-run).
"""

import hashlib
import os
import re
import shutil
import stat
import tarfile
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

_VERSION = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
_CHUNK = 1 << 16


class ArtifactError(Exception):
    pass


def _download(url, dest, sha256, size, opener):
    digest = hashlib.sha256()
    received = 0
    with opener(url, timeout=60) as resp, open(dest, "wb") as out:
        while True:
            chunk = resp.read(_CHUNK)
            if not chunk:
                break
            received += len(chunk)
            if received > size:
                raise ArtifactError(f"tải quá {size} byte — file không khớp manifest")
            digest.update(chunk)
            out.write(chunk)
        out.flush()
        os.fsync(out.fileno())
    if received != size:
        raise ArtifactError(f"nhận {received} byte, manifest nói {size}")
    if digest.hexdigest() != sha256.lower():
        raise ArtifactError("SHA-256 không khớp manifest")


def _safe_target(base, name):
    target = (base / name).resolve()
    if base.resolve() not in target.parents and target != base.resolve():
        raise ArtifactError(f"đường dẫn thoát ra ngoài thư mục cài: {name}")
    return target


def _extract(archive, dest, single_name):
    if zipfile.is_zipfile(archive):
        with zipfile.ZipFile(archive) as z:
            for info in z.infolist():
                target = _safe_target(dest, info.filename)
                if info.is_dir():
                    target.mkdir(parents=True, exist_ok=True)
                    continue
                target.parent.mkdir(parents=True, exist_ok=True)
                with z.open(info) as src, open(target, "wb") as out:
                    shutil.copyfileobj(src, out)
                # zipfile bỏ quyền Unix; mất bit x là binary trong bundle không chạy được.
                mode = (info.external_attr >> 16) & 0o777
                if mode:
                    os.chmod(target, mode)
    elif tarfile.is_tarfile(archive):
        with tarfile.open(archive) as t:
            for member in t.getmembers():
                _safe_target(dest, member.name)
                if member.issym() or member.islnk():
                    raise ArtifactError(f"không nhận link trong gói: {member.name}")
            if hasattr(tarfile, "data_filter"):  # Python có bộ lọc an toàn (3.12+, bản vá 3.8+)
                t.extractall(dest, filter="data")
            else:
                t.extractall(dest)
    else:
        shutil.copy2(archive, dest / single_name)


def _point(link, target):
    tmp = link.with_name(f".{link.name}.tmp")
    if tmp.is_symlink() or tmp.exists():
        tmp.unlink()
    os.symlink(target, tmp)
    os.replace(tmp, link)  # nguyên tử: không có lúc nào `current` vắng mặt


def current_version(root):
    link = Path(root) / "current"
    return Path(os.readlink(link)).name if link.is_symlink() else None


def _marker(releases, version):
    # Ngoài thư mục bản phát hành: bundle có thể bị chương trình đọc nó từ chối vì
    # có file lạ.
    return releases / f".{version}.sha256"


def _prune(root):
    """Giữ đúng current + previous; bản cũ hơn chỉ tốn đĩa (rollback chỉ dùng previous)."""
    releases = root / "releases"
    keep = {Path(os.readlink(root / n)).name for n in ("current", "previous") if (root / n).is_symlink()}
    for d in releases.iterdir() if releases.is_dir() else ():
        if d.is_dir() and d.name not in keep:
            shutil.rmtree(d, ignore_errors=True)
            _marker(releases, d.name).unlink(missing_ok=True)


def install(url, sha256, size, root, version, validate=None, opener=None, allow_http=False):
    """Tải + kiểm + giải nén vào releases/<version>, chạy validate(dir), rồi chuyển
    `current`. Cài lại đúng bản đang chạy = không làm gì. Trả về thư mục bản đó."""
    root = Path(root)
    if not _VERSION.match(str(version)):
        raise ArtifactError("version chỉ gồm [A-Za-z0-9._-]")
    if not url.startswith("https://") and not allow_http:
        raise ArtifactError("chỉ nhận URL https")
    if not re.fullmatch(r"[0-9a-fA-F]{64}", sha256 or "") or int(size) <= 0:
        raise ArtifactError("manifest thiếu sha256/size hợp lệ")
    sha256 = sha256.lower()

    releases = root / "releases"
    release = releases / version
    marker = _marker(releases, version)
    if current_version(root) == version:
        # Lệnh cài bị gửi lại cho bản đang chạy: không tải, không kiểm lại, và tuyệt
        # đối không xoá — thư mục này đang được controller dùng.
        return release

    created = False
    if release.is_dir():
        if not marker.exists() or marker.read_text().strip() != sha256:
            raise ArtifactError(f"releases/{version} đã có với nội dung khác — đổi version")
    else:
        staging = root / ".staging"
        staging.mkdir(parents=True, exist_ok=True)
        part = staging / f"{version}.part"
        unpack = staging / version
        shutil.rmtree(unpack, ignore_errors=True)
        try:
            _download(url, part, sha256, int(size), opener or urllib.request.urlopen)
            unpack.mkdir()
            name = Path(urllib.parse.urlparse(url).path).name or version
            _extract(part, unpack, name)
            releases.mkdir(parents=True, exist_ok=True)
            os.replace(unpack, release)
            marker.write_text(sha256)
            created = True
        finally:
            part.unlink(missing_ok=True)
            shutil.rmtree(unpack, ignore_errors=True)

    if validate is not None:
        try:
            validate(release)
        except Exception as e:
            if created:  # chỉ xoá thứ chính lần gọi này vừa tạo
                shutil.rmtree(release, ignore_errors=True)
                marker.unlink(missing_ok=True)
            raise ArtifactError(f"bản {version} không qua kiểm tra: {e}") from e

    current = root / "current"
    if current.is_symlink():
        _point(root / "previous", os.readlink(current))
    _point(current, Path("releases") / version)
    _prune(root)
    return release


def rollback(root):
    """Đổi `current` về `previous` (và ngược lại). Trả về version đang chạy sau đổi."""
    root = Path(root)
    current, previous = root / "current", root / "previous"
    if not previous.is_symlink():
        raise ArtifactError("không có bản trước để quay lại")
    cur_target, prev_target = os.readlink(current), os.readlink(previous)
    _point(current, prev_target)
    _point(previous, cur_target)
    return Path(prev_target).name


def is_executable(path):
    return Path(path).is_file() and bool(os.stat(path).st_mode & stat.S_IXUSR)
