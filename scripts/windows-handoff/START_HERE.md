# s3g-dsp — Windows source and test handoff

1. Extract the complete handoff ZIP to a short local path such as `C:\s3g`.
2. Open PowerShell in this folder and verify the payload:

   ```powershell
   .\VERIFY_HANDOFF.ps1
   ```

   The script only reads files and checks SHA-256. If Windows blocks it, review
   it and follow your normal local script policy; do not globally disable script
   protections. You can also compare individual files with `Get-FileHash -Algorithm
   SHA256` against `SHA256SUMS.txt`. Verification is for transfer integrity, not
   proof of publisher trust. The separate outer ZIP `.sha256` verifies the archive.
3. Open `s3g-dsp/docs/installing-plugins.html` for Windows installation guidance
   and follow the README inside the selected test package. Compilers are not
   required to test prebuilt plug-ins.
4. For source work, open **`s3g-dsp`** in VS Code with the official OpenAI Codex
   extension, sign in, start a new conversation and paste `CODEX_START_PROMPT.txt`.

Contents:

- `s3g-dsp/`: source snapshot, public documentation and build/test scripts;
  no `.git` or private development notes. For revisions intended for integration,
  use a separate Git clone and a short-lived branch; preserve the snapshot as
  the testing baseline and review changes before merging.
- `windows-test-packages/`: latest full suite (121 CLAPs, excluding Ambi Energy)
  and a Tracker-only alternative. **Install only one Tracker copy.**
- `SOURCE_SNAPSHOT.json`: base commit, working-tree status, source file hashes
  and the deliberately omitted external `max` symlink.
- `SHA256SUMS.txt`, `VERIFY_HANDOFF.ps1`: payload integrity check.
- `CODEX_START_PROMPT.txt`: concise context for the Windows agent.

Windows 10 / i7-4510U / 8 GB is the acceptance target. The Tracker correction
needs a native check; Point/Pyrosphere performance remains unresolved. These are
cross-built test packages, not an accepted Windows release. Nothing in this kit
automatically installs plug-ins, changes Git history or transfers account data.
