#!/usr/bin/env python3
"""EXE'nin PE üst verisini doğrular: x64, GUI alt sistemi, manifest (requireAdministrator, PerMonitorV2), sürüm bilgisi,
ağ API'si içe aktarımı YOK, ASLR/DEP açık. Kullanım: verify_pe.py MonsterGpuPower.exe [--demo]"""
import re, sys, pefile

def main():
    path = sys.argv[1]
    demo = "--demo" in sys.argv
    pe = pefile.PE(path)
    ok = True
    def check(name, cond, info=""):
        nonlocal ok
        print(("[OK]   " if cond else "[FAIL] ") + name + (" -> " + info if info else ""))
        ok &= bool(cond)
    check("Makine x86-64", pe.FILE_HEADER.Machine == 0x8664)
    check("Alt sistem GUI (konsol penceresi yok)", pe.OPTIONAL_HEADER.Subsystem == 2, str(pe.OPTIONAL_HEADER.Subsystem))
    dll = pe.OPTIONAL_HEADER.DllCharacteristics
    check("ASLR (DYNAMIC_BASE)", bool(dll & 0x40)); check("DEP (NX_COMPAT)", bool(dll & 0x100)); check("High-entropy VA", bool(dll & 0x20))
    imports = {e.dll.decode().lower(): [i.name.decode() for i in e.imports if i.name] for e in pe.DIRECTORY_ENTRY_IMPORT}
    print("       içe aktarılan DLL'ler:", ", ".join(sorted(imports)))
    net = [d for d in imports if d in ("ws2_32.dll", "wsock32.dll", "wininet.dll", "winhttp.dll", "urlmon.dll", "mswsock.dll", "iphlpapi.dll")]
    check("Ağ DLL'si içe aktarımı yok", not net, ",".join(net))
    funcs = {f for v in imports.values() for f in v}
    bad = sorted(f for f in funcs if f in ("URLDownloadToFileW", "InternetOpenW", "WinHttpOpen", "WSAStartup", "ReadMEMB", "WritePCI"))
    check("Ağ / OEM-DLL dışa aktarımı çağrısı yok", not bad, ",".join(bad))
    raw = open(path, "rb").read()
    check("OEM ACPIDriverDll.dll adı ikilide geçmiyor", b"ACPIDriverDll" not in raw and "ACPIDriverDll".encode("utf-16le") not in raw)
    check("PowerShell/RWEverything adı geçmiyor", b"powershell" not in raw.lower() and "rweverything".encode("utf-16le") not in raw.lower())
    m = None
    for e in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if e.id == 24:
            d = e.directory.entries[0].directory.entries[0].data.struct
            m = pe.get_data(d.OffsetToData, d.Size).decode("utf-8", "replace")
    check("Manifest gömülü", m is not None)
    if m:
        check("requireAdministrator", 'level="requireAdministrator"' in m)
        check("PerMonitorV2 DPI", "PerMonitorV2" in m)
    ver = {}
    for fi in getattr(pe, "FileInfo", []) or []:
        for f in fi:
            if hasattr(f, "StringTable"):
                for st in f.StringTable:
                    ver.update({k.decode(): v.decode() for k, v in st.entries.items()})
    check("Sürüm bilgisi", ver.get("ProductName") == "MONSTER GPU POWER", str(ver.get("FileVersion")))
    has_demo_marker = (b"DEMO" in raw) or ("DEMO".encode("utf-16le") in raw) or (b"DemoRig" in raw) or (b"MGP_DEMO" in raw)
    check("Üretim EXE'sinde demo/mock kodu yok" if not demo else "Demo EXE'si demo işareti taşıyor",
          (not has_demo_marker) if not demo else has_demo_marker)
    check("Üretim EXE'sinde 'MGP_DEMO' sembol/dize yok" if not demo else "(demo)", demo or b"DemoRig" not in raw)
    # Demo-yalnız sahte donanım / hata enjeksiyonu yolları (DemoRig, DemoNv, MGP_DEMO_* ortam değişkenleri): ASCII ve UTF-16LE.
    markers = ["DemoRig", "DemoNv", "MGP_DEMO", "MonsterGpuPower.Demo"]
    found = [t for t in markers if t.encode() in raw or t.encode("utf-16le") in raw]
    if demo:
        check("(demo) demo EXE'si sahte-donanım/enjeksiyon dizgelerini taşıyor (denetimin kendisi çalışıyor)", bool(found), ",".join(found))
    else:
        check("Üretim EXE'sinde demo sahte-donanım/enjeksiyon dizgeleri yok (DemoRig, DemoNv, MGP_DEMO*)", not found, ",".join(found))
    print("Boyut: %d bayt  SHA-256:" % len(raw), __import__("hashlib").sha256(raw).hexdigest())
    sys.exit(0 if ok else 1)
main()
