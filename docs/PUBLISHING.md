# Publication — owner action only

No repository or site is created by the build. Replace `OWNER` and `REPO` below with your chosen public destination, inspect the source tree and license/audio notices, and run these commands only when you authorize publication:

```powershell
git init -b main
git add .
git commit -m "FM1 DRUM OS 1.0.0-rc.2"
gh repo create OWNER/REPO --public --source . --remote origin --push
```

In that repository's Settings → Pages, select **GitHub Actions** as source. Then, when site deployment is authorized:

```powershell
gh workflow run editor-pages.yml --repo OWNER/REPO -f publish=true
```

The manual workflow defaults `publish=false`. It installs from `drum-os-editor/package-lock.json`, typechecks/tests/builds `drum-os-editor/`, uploads `drum-os-editor/dist` and deploys only when explicitly enabled. Vite base `./` supports `https://OWNER.github.io/REPO/`. Keep the firmware/editor in this one repository; do not create a second editor repository.

Release FWSC/BIN/ELF, SHA-256 and release notes belong in a separately reviewed release bundle, not ordinary Git source history. This candidate remains hardware-pending. No Git initialization, remote creation, push or workflow execution was performed by the source export.
