# Driver interface (ABI), EC map and the limits of independence

**English** · [Türkçe](DRIVER_ABI.tr.md)

This document records how the application reaches the EC and **what that rests on** (with an evidence level).
Evidence sources: the device owner's own measurements and an inspection of the ReadEC/WriteEC machine code in the manufacturer's `ACPIDriverDll.dll`
(OEM binaries are **not** part of this repository). The single central constants file: [`src/core/ec_constants.h`](../src/core/ec_constants.h).

## 1. Device and IOCTLs (read from the OEM DLL's ReadEC/WriteEC machine code)

| Item | Value |
|---|---|
| Device path | `\\.\ACPIDriver` (`CreateFileW`, GENERIC_READ\|WRITE) |
| ReadEC | `DeviceIoControl` code `0x9C40A488` (DeviceType 0x9C40, METHOD_BUFFERED, FILE_ANY_ACCESS/RW) |
| WriteEC | `DeviceIoControl` code `0x9C40A48C` |
| Buffer | input and output are the **same pointer**, both **0x400000 (4 MiB)** bytes; content is `{DWORD address; DWORD value;}` |
| Read result | the first byte of the output buffer |
| A bug in the OEM DLL | it ignores the `DeviceIoControl` return value; on a failed call the low byte of the address left in the buffer is read as a "stale echo" (a fake 0x43/0x44 state) |

How the application handles these: the return value and `bytesReturned >= 1` are checked; a read result is accepted only if the IOCTL succeeded;
only 0x0743–0x0746 are read, only allow-listed (address, value) pairs are written (`IsAllowedEcWrite`); the buffer is zeroed on every call.

**Unverified assumption:** it is not known whether the driver accepts a smaller buffer, so **exactly the same 4 MiB** as the OEM DLL is used.
Every call allocates/frees 4 MiB of temporary memory (about one call pair per second while polling; negligible load).

## 2. EC map

| Address | Meaning | Application |
|---|---|---|
| `0x0743` | cTGP/DB control byte. `0x01` = OEM AutomaticOverboost OFF (125 W), `0x05` = ON (bit0 general + bit2 cTGP) | reads + **writes** (only 0x01/0x05) |
| `0x0744` | cTGP increase (W), multiples of 5, 0..25 | reads + **writes** |
| `0x0745` | TPP | read-only (expected 0) |
| `0x0746` | "MAX_TGP" in the Linux driver | read-only (expected 0) |

Profile mapping and evidence level:

| Profile | 0x0743 | 0x0744 | Evidence |
|---|---|---|---|
| 125 W | 0x01 | 0 | measured live with OEM off |
| 130 W | 0x05 | 5 | **expected mapping, not tried live** |
| 135 W | 0x05 | 10 | **expected mapping, not tried live** |
| 140 W | 0x05 | 15 | measured live with OEM on |
| 145 W | 0x05 | 20 | EC + NVIDIA verified (idle polling) |
| 150 W | 0x05 | 25 | EC + NVIDIA verified, ~150 W measured under load |

The `0x0743` bit names are taken from the Linux `uniwill-acpi.c` macros (naming only; Linux does not write this byte for this purpose).
Write order: when going up, the control byte first (with offset 0 = 125 W), then the offset; when going down, the offset first, then the control byte.

## 3. Driver identity and the limits of dependencies

* The kernel driver is Uniwill's **`ACPIDriver`** (PnP/INF) driver; its hardware ID is **`ACPI\INOU0000`**. It is a **separate** driver package from Control Center;
  Control Center itself (`GCUService`) also checks that the driver exists.
* To reach the EC this application **requires the `ACPIDriver` kernel driver to be installed and running**. The driver's `.sys/.inf` files are **not** in this repository
  and cannot be redistributed (they belong to the manufacturer). Without the driver the application does not write to the EC and says why.
* Control Center's `ACPIDriverDll.dll` is not needed for EC access: the application opens the device directly with `DeviceIoControl` (based on the machine-code analysis).
* **For persistence**, however, Control Center's `GCUService` service is required: every profile selection is also saved to the OEM profile file, and if the OEM profile file
  is not usable **nothing is applied** (see the [README](../README.md#what-does-persistence-rely-on)). So the application is not "completely independent of Control Center".
* There is **no** signature-enforcement bypass / test-signing trick / driver-loading trick, and none is recommended.
