# Device acceptance test (v1.5.0) — on your own device

**English** · [Türkçe](DEVICE_ACCEPTANCE_TEST.tr.md)

> **YOU will do this list, on your own Monster Tulpar T5 V21.7.** The application has not yet been run on real hardware; none of these steps was carried out by the author on a real device.
> Fill in the result yourself; do **not assume** a step that did not pass has passed. If a step fails, open an [Issue](../../../issues) and attach the output and `%LOCALAPPDATA%\MonsterGpuPower\logs\mgp.log`.
>
> Out of scope (do NOT do these in this test): FurMark / Cinebench / full CPU+GPU load; the 230 W adapter policy and the 330 W limitation; ACPI battery behaviour (this project does not claim to solve them).
> The PowerShell commands below run in an **administrator** window and only **read / copy**.

The same three verifications are used in every step, shortly **(J) (E) (N)**:

| | How to look | Expected (for 150 W) |
|---|---|---|
| **(J)** OEM JSON | `Select-String -Path "$d\Mode3_Profile1.json" -Pattern ConfigurableTGPTarget` | `"ConfigurableTGPTarget": 150` |
| **(E)** EC | *EC 0x0743 / 0x0744* in the application's top panel | `0x05 / 25 W` |
| **(N)** NVIDIA | `nvidia-smi -q -d POWER` → *Current Power Limit* | `150.00 W` |

Common variable (once in every PowerShell window):

```powershell
$d = 'C:\Program Files\OEM\Control Center\UniwillService\MyControlCenter\UserPofiles'
```

The interface language (EN | TR switch, installer language page) does not change any step below; the quoted messages exist in both languages — the English wording is given here, the Turkish wording is in [DEVICE_ACCEPTANCE_TEST.tr.md](DEVICE_ACCEPTANCE_TEST.tr.md).

## 0. Preparation: backup and baseline record (BEFORE opening the application)

1. The GCUService version must be **1.0.2.70**:
   `(Get-Item 'C:\Program Files\OEM\Control Center\UniwillService\MyControlCenter\GCUService.exe').VersionInfo.FileVersion`
   If it differs, the application refuses to write anyway; stop the test and open an Issue.
2. **Back up** the OEM folder separately:
   ```powershell
   $b = "$env:USERPROFILE\Desktop\OEM_UserPofiles_BACKUP_$(Get-Date -Format yyyyMMdd-HHmmss)"
   New-Item -ItemType Directory $b | Out-Null
   Copy-Item "$d\*" $b -Force
   Get-ChildItem $b | Select-Object Name, Length
   ```
3. Take and keep a baseline record (for comparison):
   ```powershell
   Get-ChildItem $d -File | Select-Object Name, Length, LastWriteTime | Format-Table -AutoSize
   Get-ChildItem $d -File | Get-FileHash -Algorithm SHA256 | Select-Object @{n='File';e={Split-Path $_.Path -Leaf}}, Hash | Format-Table -AutoSize
   ```
4. Note the tasks in Task Scheduler (before installing):
   ```powershell
   schtasks /query /tn MonsterGpuPower_ApplyAtLogon   # EXISTS if you installed "apply at logon" in v1.0–v1.3 (an error = it does not exist)
   schtasks /query /tn MonsterGpuPower_Autostart      # EXISTS if "start at login" is on
   ```

## 1. Installation, ZERO writes on first open

1. Install with `MONSTER_GPU_POWER_Setup.exe` (compare the SHA-256 with `SHA256SUMS.txt`). Pick your language on the installer's first page. If the old `MonsterGpuPower_ApplyAtLogon` task exists, the installer **deletes** it (it applies no profile);
   it does not touch the OEM folder.
2. Re-run the commands in 0.3. **Pass:** the file list, sizes, `LastWriteTime` and SHA-256 values are **exactly the same**; no new `.bak`. ☐ Pass ☐ Fail
3. Open the application. The status strip should show the device identity as *verified* and the driver as *Connected*; the *PERSISTENT APPLY* box should show the real value in the file
   (e.g. `OEM profile holds 140 W · Live profile: 140 W (OEM default)`). Merely opening must write nothing; repeat 0.3 and verify. The window must be in the language you chose in the installer. ☐ Pass ☐ Fail

## 2. First persistent profile (suggested order: 145 → 150)

1. Click **145 W**; read and confirm the risk confirmation. (If the file holds a value other than 140 W — e.g. from earlier manual experiments — the "foreign value" box appears first; answer it as in section 5.)
2. The result line must report the two layers: *(a) live: the EC was read back, NVIDIA confirmed 145 W*; *(b) OEM profile file: ConfigurableTGPTarget = 145 was written and read back*.
3. Verify: **(J)** `145`; EC `0x05 / 20 W`; NVIDIA `145.00 W`.
4. There must be **exactly 1 new** `Mode3_Profile1.json.mgp-original-<date>-<code>.bak` in `$d`, and its content must be byte-for-byte identical to the original file from 0.3 (compare SHA-256). The backup must be read-only.
5. `Mode3_Profile1.json` must differ from the original file only in the `ConfigurableTGPTarget` value (e.g. with `fc.exe` or a diff tool). ☐ Pass ☐ Fail
6. **Click the same profile again**: there must be no write and no new `.bak` (an "already active / the OEM profile already …" message). ☐ Pass ☐ Fail
7. Then select **150 W**; do the same verifications (150 / `0x05 / 25 W` / `150.00 W`); this time there must be 2 backups in total. ☐ Pass ☐ Fail

## 3. Persistence after a restart (the most important step)

1. Exit the application with **X** (do not minimise to the tray; "Return to OEM 140 W on exit" must be **off**).
2. **Restart** the computer. WITHOUT opening the application, verify (J), (E), (N). ☐ Pass ☐ Fail
3. Repeat after at least **two** normal restarts and a few screen off/on cycles. The application cannot know **when** GCUService reads the file; this test answers that question on your device.
4. If the result does not hold (e.g. it returns to 140 W), open an Issue; do **not** call it "successful", because this application cannot verify the effect after a restart.

## 4. Other profiles

Do the verifications of step 2 for 125, 130, 135 and 140 W as well. **130 and 135 W are mappings that have not been tried live**: after writing, the application reads the EC back, verifies NVIDIA and
returns to the previous value if it does not hold; still, leave these steps for **last**. Reboot persistence (step 3) has not been tried separately for every profile. ☐ Pass ☐ Fail

## 5. Foreign-value protection

1. Close the application. Change the target in `Mode3_Profile1.json` **by hand** (as administrator, with your backup in place) to another allowed value (e.g. 130) and open the application.
2. Select a profile: the box *"The OEM profile file currently holds 130 W, and this application did not write that value…"* must appear. Say **No** → nothing may change (neither the live value nor the file). ☐ Pass ☐ Fail
3. Select again, this time say **Yes** → the live value must be applied and verified first, then the file must be backed up and changed. ☐ Pass ☐ Fail

## 6. Settings

* **Start the app at login:** turn it on; `schtasks /query /tn MonsterGpuPower_Autostart` must show the task. Log in again: the application must start **in the tray** after ~10 s and **apply no profile**
  (verify with the 0.3 commands that the files did not change). Turning it off must delete the task. ☐ Pass ☐ Fail
* **Return to OEM 140 W on exit:** turn it on, select a profile, exit the application: the live value must be 140 W (NVIDIA `140.00 W`), the OEM **file must not change** (your selected value must stay). ☐ Pass ☐ Fail
* **EN | TR:** click the other half of the switch; every label changes immediately; close and reopen the application: your choice is kept. ☐ Pass ☐ Fail

## 7. Rollback

Select **140 W** in the application, or copy `Mode3_Profile1.json` back from the `$b` backup / a `.bak` file. None of the backups is deleted by the application.

## 8. Uninstall

Run the uninstaller: the `MonsterGpuPower_Autostart` and `MonsterGpuPower_ApplyAtLogon` tasks must be deleted; the OEM folder and the backups **must not change** (verify with the 0.3 commands). ☐ Pass ☐ Fail
