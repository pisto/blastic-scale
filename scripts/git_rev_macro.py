#!/bin/env python

import subprocess

revision = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).decode("utf-8").strip()
status = "clean" if not subprocess.check_output(["git", "status", "--porcelain"]).decode("utf-8").strip() else "dirty"
print(f"'-DBLASTIC_GIT_COMMIT=\"{revision}\"' '-DBLASTIC_GIT_WORKTREE_STATUS=\"{status}\"'")
