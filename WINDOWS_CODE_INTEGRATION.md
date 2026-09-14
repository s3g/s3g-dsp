# s3g-dsp Windows code integration instructions for Codex

Use these instructions when source revisions are needed on the Windows test
machine. The goal is to keep macOS and Windows work in one Git history, with
short-lived branches that can be reviewed and merged without copying whole
source folders between machines.

## Non-negotiable rule: test snapshot versus working clone

The existing Windows handoff ZIP contains a source snapshot without `.git`.
That snapshot is useful for reproducing the supplied binaries and preserving
their exact baseline, but it must not become the long-lived Windows source
checkout.

The current handoff identifies commit
`892e0af704b5316fce6b1c388f96f6c49ed2389b` as its baseline. The live GitHub
repository may already be newer. Do not copy an edited snapshot over the Mac
repository and do not initialize an unrelated repository in the snapshot for
changes that are intended to merge upstream.

Use two separate directories:

- Keep the extracted handoff unchanged for baseline testing and comparison.
- Make all source revisions in a fresh authenticated clone of the real GitHub
  repository.

Before source work begins, confirm with the user that current Mac changes have
been committed and pushed. A Windows branch cannot include unpushed Mac work.

## Create the Windows working clone

Install Git for Windows if necessary, then use an ordinary PowerShell session:

```powershell
git clone https://github.com/s3g/s3g-dsp.git C:\src\s3g-dsp
Set-Location C:\src\s3g-dsp
git config core.autocrlf false
git fetch origin
git switch main
git pull --ff-only
git status --short
git log -1 --oneline
git remote -v
```

`git status --short` must be empty before starting a branch. The repository's
`.gitattributes` normalizes text to LF; keeping `core.autocrlf` false avoids a
whole-tree line-ending rewrite on Windows.

Create one short-lived branch for one bounded issue. For example:

```powershell
git switch -c windows/tracker-page-fix origin/main
```

Use a descriptive name such as `windows/tracker-page-fix` or
`windows/point-editor-performance`. Do not maintain permanent Mac and Windows
development branches.

## Reproduce before editing

When a prebuilt handoff package exists, test it from the unchanged handoff
directory before compiling a replacement. Record:

- Exact source baseline and plug-in/package SHA-256.
- Windows build, REAPER version, display scaling, audio driver, sample rate and
  buffer size.
- Minimal reproduction steps, expected result and actual result.
- Checker command, exit code and complete log.
- Screenshots or measurements needed to distinguish GUI, DSP and memory issues.

Do not make a speculative source fix until the remaining failure has been
reproduced or the evidence establishes that the supplied build already fixes
it. GitHub Actions and mock-host tests do not replace interactive Windows 10 and
REAPER acceptance.

## Make revisions without creating platform drift

- Keep shared behavior in shared code and shared tests.
- Use `_WIN32` in C++ and `WIN32` in CMake only for genuinely platform-specific
  implementation details.
- Prefer the repository's existing platform implementation files over adding
  large conditional blocks to shared code.
- Preserve stable CLAP identities, parameter IDs, saved-state formats, GUI
  behavior and documented routing unless the task explicitly changes them.
- Add or update a regression test with a bug fix whenever practical.
- Never commit build directories, generated Visual Studio projects, downloaded
  dependencies, test packages, user settings, logs or installed plug-ins.
- Never overwrite a plug-in currently loaded by REAPER.
- Do not use destructive Git commands or force-push unless the user explicitly
  authorizes the exact action.

Inspect the change frequently:

```powershell
git status --short
git diff --check
git diff --stat
git diff
```

Stage specific paths rather than staging the entire working tree blindly:

```powershell
git add plugins/clap_tracker/s3g_tracker_windows_editor.cpp
git add tests/tracker_windows_clap_smoke.cpp
git diff --cached --check
git diff --cached
```

Keep commits small and cohesive. A useful commit contains one explained change,
its test, and any directly required documentation. Codex must obtain the user's
approval before creating commits, pushing a branch or opening a pull request.

Example, after approval:

```powershell
git commit -m "Fix Tracker page activation on Windows"
git push -u origin windows/tracker-page-fix
```

## Keep the branch current

Before a substantial new edit and before requesting integration, bring current
`main` into the Windows branch:

```powershell
git fetch origin
git status --short
git merge origin/main
```

The working tree must be clean before the merge. Merging is the conservative
default because it does not rewrite already shared commits. Rebasing is suitable
only for an unpublished personal branch and only when the user requests it.

Git normally merges changes made on different lines automatically. If both
machines changed the same lines, resolve the conflict by understanding both
intentions. Do not accept `ours` or `theirs` for a complete file merely to make
the conflict disappear. Search for conflict markers afterward and rerun the
relevant tests:

```powershell
git grep -n "<<<<<<<\|=======\|>>>>>>>"
git diff --check
```

## Build and test on Windows

Use the task's documented native Windows configuration. Do not reuse a Mac,
MinGW or differently configured build directory. For the focused Tracker path,
the current handoff uses:

```powershell
cmake -S . -B build-tracker-windows -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON -DS3G_BUILD_CLAP_PLUGIN=ON -DS3G_BUILD_TRACKER_PREVIEW=ON -DS3G_ENABLE_PORTABLE_CLAP_GUI=ON -DS3G_BUILD_STANDALONE_APPS=OFF -DS3G_BUILD_FUTURE_COMPONENTS=OFF -DS3G_ENABLE_WORLD=OFF
cmake --build build-tracker-windows --config Release --parallel 2 --target s3g_tracker_windows_clap_smoke s3g_tracker_portable_core_tests s3g_tracker_workspace_layout_tests s3g_tracker_grid_selection_tests
ctest --test-dir build-tracker-windows -C Release -R "^s3g_tracker_" --output-on-failure
```

The exact configuration may require the separately checked-out CLAP test
headers described in `.github/workflows/tracker-windows-clap.yml`. Use the pins
and options already established by the repository rather than silently changing
dependency versions.

After automated checks, repeat the original manual REAPER reproduction with the
new binary. Record failed tests as failures; do not describe an unrun native,
audio or GUI check as passing.

## Integrate through a pull request

The preferred integration route is:

1. Push the short-lived Windows branch.
2. Open a pull request into `main`.
3. Review the complete source diff and test evidence.
4. Let the applicable GitHub Actions checks finish.
5. Merge the pull request only after Windows acceptance and Mac-impact review.
6. Update both machines from the merged `main`.

The existing Tracker workflows provide useful coverage:

- `tracker-portable-core.yml` runs the shared Tracker core on macOS and Windows.
- `tracker-windows-clap.yml` builds and exercises the native Windows Tracker
  CLAP integration path.

These workflows are path-scoped and do not cover every s3g-dsp product. For a
change outside their paths, identify and run appropriate Mac and Windows tests
before merging. `windows-latest` CI also does not substitute for the established
Windows 10/REAPER test machine.

After the pull request is merged, update each checkout:

```powershell
git switch main
git pull --ff-only
```

Start the next issue from a new branch based on the updated `origin/main`.

## Offline or no-push fallback

If the Windows machine can clone and build but must not push, still work in a
real clone and make local commits. After approval, export those commits:

```powershell
New-Item -ItemType Directory -Force windows-patches | Out-Null
git format-patch --binary origin/main..HEAD --output-directory windows-patches
```

Copy the numbered patch files to the Mac. From a clean Mac checkout, create an
integration branch and apply them in order:

```sh
git fetch origin
git switch -c integrate/windows-fix origin/main
git am --3way /path/to/windows-patches/*.patch
```

Review and test the resulting branch exactly as if it came from a pull request.
Do not apply patches directly over unrelated uncommitted Mac changes.

If edits were accidentally made in the no-Git snapshot, stop editing and keep
both an untouched snapshot and the edited copy. Create a fresh real clone at the
recorded baseline, copy only the intentionally changed files into a new branch,
review the resulting diff, and commit there. Never copy the snapshot's `.git`
directory—if one was initialized—into the Mac repository, and never replace the
Mac working tree wholesale.

## Required handback from Windows Codex

Return a concise integration report containing:

```text
Issue and reproduction:
Repository URL:
Base commit:
Branch:
Resulting commit(s):
Files changed:
Build commands and exit results:
Automated tests and results:
Manual Windows 10 / REAPER checks:
Binary or package SHA-256 values:
Known limitations or unrun checks:
Pull-request URL or patch-file list:
```

Also include these read-only summaries:

```powershell
git status --short
git log --oneline origin/main..HEAD
git diff --stat origin/main...HEAD
```

The desired final state is one shared `main` branch containing both portable and
platform-specific implementations, with Windows branches disappearing after
their reviewed changes are integrated.
