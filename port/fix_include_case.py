# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Rewrite #include paths to the real on-disk case and use forward slashes.
Windows resolves includes case-insensitively; Linux does not. Search order mimics MSVC for "":
the including file's directory first, then every project's include directories."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import vcx  # noqa: E402

INC_RE = re.compile(r'^(\s*#\s*include\s*)([<"])([^>"]+)([>"])', re.M)
EXTS = (".cpp", ".c", ".h", ".hpp", ".inl", ".inc")


def main(dry_run=False):
    roots = []
    for p in vcx.projects():
        for d in p["includes"]:
            if d not in roots:
                roots.append(d)
    idx = vcx.ci_index()
    changed_files = fixed = slashes = 0
    for dp, dn, fn in os.walk(vcx.ROOT):
        if ".git" in dp.split(os.sep) or os.sep + "port" in dp:
            continue
        for f in fn:
            if not f.lower().endswith(EXTS):
                continue
            path = os.path.join(dp, f)
            here = os.path.relpath(dp, vcx.ROOT)
            text = open(path, encoding="latin-1", newline="").read()   # keep CRLF as-is

            def fix(m):
                nonlocal fixed, slashes
                inc = m.group(3).replace("\\", "/")
                if inc != m.group(3):
                    slashes += 1
                for root in [here] + roots:
                    cand = os.path.normpath(os.path.join(root, inc))
                    if os.path.exists(os.path.join(vcx.ROOT, cand)):
                        break                                   # exact match: keep as written
                    real = idx.get(cand.lower())
                    if real:
                        # keep the include's own prefix form (../ etc.), fix the case of the tail
                        prefix = [s for s in inc.split("/") if s in ("..", ".")]
                        k = len(inc.split("/")) - len(prefix)          # path segments after any ../ prefix
                        new = "/".join(prefix + real.split(os.sep)[-k:])
                        if new.lower() == inc.lower() and new != inc:
                            fixed += 1
                            inc = new
                        break
                return f"{m.group(1)}{m.group(2)}{inc}{m.group(4)}"

            new_text = INC_RE.sub(fix, text)
            if new_text != text:
                changed_files += 1
                if not dry_run:
                    open(path, "w", encoding="latin-1", newline="").write(new_text)
    print(f"{'(dry run) ' if dry_run else ''}files changed: {changed_files}, case fixes: {fixed}, backslash fixes: {slashes}")


if __name__ == "__main__":
    main(dry_run="--dry-run" in sys.argv)
