# Test and validation status (v1.5.0)

**English** · [Türkçe](TEST_STATUS.tr.md)

> **Summary: the application has not yet been run on real hardware.** Every "passed" result below is either a pure-logic unit test or a **simulation**
> (under Wine, with a fake EC / NVIDIA / SMBIOS / OEM folder). A simulation is not a substitute for real Windows, real NTFS/ACL/`ReplaceFileW` behaviour,
> the real Task Scheduler, the real `GCUService` or real hardware.

## 1. Tests that were run

| Layer | Result | What it proves | What it does not prove |
|---|---|---|---|
| Unit tests (`make test`, g++ + ASan + UBSan, on the host) | **110 tests / 2250 checks passed** | JSON parsing/patching (including malformed and hostile input), the safe-write algorithm and TOCTOU re-check, transaction classification (`BothOk`, `OemFailedLiveRestored` …), ownership/foreign-value rules, identity check, task XML, that UI texts never say "EC firmware/BIOS persistently written", **language resolution (settings → installer → Windows language → English), and that every UI text exists in both languages and the English is not a copy of the Turkish** | Real Windows/hardware |
| Wine + fake hardware, end-to-end GUI (`MonsterGpuPower_DEMO.exe`) | **211 checks passed, 0 failed** | With real Win32 window/message/file calls: zero writes on first open; each of the 7 profile changes is instantly persistent (EC + NVML + OEM file + backups); clicking the same profile again; foreign-value confirmation (No, and SHA-pinned Yes); nothing applied in locked OEM states; the live layer rolled back when the OEM write fails; the OEM file left untouched when the live layer fails; confirmation boxes; *start at login*; *return to OEM 140 W on exit*; *About*; command-line modes; old settings keys having no effect; corrupt OEM files; **language auto-detection (installer value, Windows language, user choice and their precedence), the EN \| TR switch (every label changes at once, the last result message is re-rendered, the choice is saved and survives a restart), English UI/dialog texts without Turkish letters, and the 96–192 DPI layout in both languages, also with the production EXE for the language path** | Real EC/NVIDIA/OEM file system; the real Task Scheduler (a file-based fake store was used) |
| Installer, Wine (`silent install + upgrade from older versions + interactive wizard`) | **silent install 30/30, upgrade from v1.3.0 15/15, upgrade from v1.4.0 14/14, interactive wizard with the language page 4/4 per language (English, Türkçe) passed** | Files, shortcuts, registry key, **the language chosen with `/LANG=EN` or `/LANG=TR` being stored (1033/1055) and becoming the application's default UI language**, deletion of an old version's task, the OEM folder staying byte-for-byte unchanged, uninstall, **the interactive wizard being driven on screen: the language page (English listed first, Türkçe second), the chosen language shown on the following pages, the registry value and the installed application opening in that language** | The installer wizard on real Windows (how the language page and wizard render there, SmartScreen behaviour) |
| PE verification (`tools/verify_pe.py`) | passed | x64, GUI subsystem, ASLR/DEP/high-entropy VA, administrator manifest, PerMonitorV2, version 1.5.0.0, no network DLL imports, no demo code/strings in the production EXE | — |

Wine test counts per section: A 19 · B 39 · C 4 · D 9 · D2 3 · E 25 · F 5 · F2 8 · F3 2 · G 6 · H 6 · H2 1 · H2b 2 · H3 2 · I 4 · I2 1 · J 4 · K 6 · K2 1 · L 4 · M 2 · N 12 · P 6 · P1b 8 · P2 10 · P3 5 · P4 7 · P5 2 · P6 6 · S 2 (total 211).
The test rigs are **not** in this repository (the compiler, Wine, Xvfb and the fake-folder rig depend on the development environment); the unit tests are under `tests/`.

## 2. NOT TRIED (real hardware and real Windows)

* **Nothing was tried on real Windows 10/11 and on a real Monster Tulpar T5 V21.7**: no real EC write and no real OEM JSON write was made with this application.
* Real Windows Task Scheduler behaviour (registration, the 10 s delayed logon trigger, deletion). Wine's partial `taskschd` emulation does not store trigger/action fields; the tests used a file-based fake store.
* **Language detection on a real Windows** (the real system UI language, the installer's registry value as written by a real Windows installer run) and the **installer's language page on real Windows**: in Wine the Windows language was simulated, the registry value lives in the Wine registry, and the NSIS wizard (language page included) was driven interactively under Wine only.
* The tray icon appearing at logon (`TaskbarCreated`, UIPI) and the behaviour of the `/minimized` mode on real Windows.
* Real NTFS/ACL/`ReplaceFileW` behaviour; **when** `GCUService` reads the profile file (only at startup or live).
* **Reboot persistence for 125, 130, 135 and 145 W.** The device owner tried the same mechanism (140 → 150 W in the JSON) with their own PowerShell script and observed that 150 W survived screen cycles and restarts; the C++ code of this application is the counterpart of that method and has not been run on a real device.
* The 130 W and 135 W EC mapping (`0x05` / 5 and 10) is the **expected** value; it has not been tried live.
* **About** opening the default browser from the elevated (administrator) process (verified in Wine only from the log that the call was made).
* `build.bat` (building on Windows) was not run; the Linux cross build and `make test` were run.
* The read-only PowerShell commands in the README were not run on a real Windows/PowerShell; only their syntax and field names were compared against the code.

## 3. Deliberately not done

The 230 W / 330 W adapter profiles, CPU power limits, fan curves, BIOS/EC firmware, ACPI battery behaviour and the [next stages](NEXT_STAGES.md) are not in this application. It is not claimed that ACPI battery problems are solved.

## 4. Reproducing

```bash
make test                                   # 110 tests / 2250 checks
make -j2 exe demo installer                 # mingw-w64 + NSIS
python3 -I tools/verify_pe.py build/MonsterGpuPower.exe
python3 -I tools/verify_pe.py build/MonsterGpuPower_DEMO.exe --demo
```

`MonsterGpuPower.exe` is reproduced byte-for-byte from the same source and toolchain (`--no-insert-timestamp`). The installer produces different bytes on a different build because of the file times NSIS embeds; its content is the same.
