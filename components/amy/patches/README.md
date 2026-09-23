# AMY firmware patch

`firmware.patch` captures the existing firmware changes against `base-revision`
(upstream 1.2.172). It does not introduce a new DSP change. It includes:

- ESP-IDF task callback signatures.
- A null check for reserved, unallocated oscillators in all-notes-off.
- Oscillator-local deterministic random streams and `amy_prng.h`.
- EQ allocation cleanup before bus reinitialization.

Python packaging changes and local extension binaries are intentionally excluded.
From the repository root, after initializing the pinned submodule:

```sh
python3 tools/amy_patches.py
```

For an existing patched checkout, use the read-only check instead:

```sh
python3 tools/amy_patches.py --check
```

Application is explicit, not a build side effect. Wrong revisions, conflicting
changes and repeated application fail; nothing resets or stashes local changes.
The check establishes patch presence, not absence of unrelated local edits.
Native CI applies the patch to a fresh submodule before compiling tests.
Review and regenerate the patch against the new base during dependency upgrades;
remove individual fixes only when upstream behavior makes them unnecessary.
