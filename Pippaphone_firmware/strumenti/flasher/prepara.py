#!/usr/bin/env python3
"""Prepara la pagina per caricare il firmware dal browser.

Compila il telefono per il LilyGO T-Deck Plus e il ripetitore per Heltec V3 e LilyGO T3-S3 (con ESP-IDF),
e la tastiera del T-Deck (ESP32-C3, con PlatformIO, se c'e'), poi mette i firmware, esptool-js e MapLibre
(la cartina per le mappe sulla microSD) dentro il modello flasher.html e scrive una pagina sola:

    build-flasher/pippaphone_flasher.html

che si apre con Chrome o Edge anche con un doppio clic, senza server e senza internet.

Uso, dalla cartella Pippaphone_firmware:
    python strumenti/flasher/prepara.py               compila (serve ESP-IDF attivo) e prepara la pagina
    python strumenti/flasher/prepara.py --solo-pagina  prepara la pagina con le compilazioni che ci sono

Senza PlatformIO (comando pio) la pagina si fa lo stesso, ma senza la tastiera: vedi
strumenti/tastiera_c3/LEGGIMI.md.
"""
import argparse
import base64
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

QUI = Path(__file__).resolve().parent
FW = QUI.parent.parent
ESPTOOL_JS = QUI / "esptool-js" / "esptool-js-0.7.0.js"
# MapLibre GL JS 4.7.1 (licenza BSD-3, maplibre/LICENSE.txt), da https://unpkg.com/maplibre-gl@4.7.1/
# (dist/maplibre-gl.js e dist/maplibre-gl.css). SHA-256, uguali a quelli della copia npm su jsDelivr:
#   maplibre-gl.js   be9633c4d870e26fb37f1cfe5c5a77181667114003ea16207ac7850d8da8add1
#   maplibre-gl.css  576b085fdd9487a65a19215328c1e086c07ce5bf6da09b666b3806d3d008dae9
MAPLIBRE = QUI / "maplibre"
USCITA = FW / "build-flasher" / "pippaphone_flasher.html"

# dispositivo -> (progetto, cartella di compilazione, file sdkconfig.defaults)
DISPOSITIVI = {
    "tdeck": ("telefono", "build-tdeck", "sdkconfig.defaults;sdkconfig.defaults.tdeck"),
    "heltec": ("ripetitore", "build-heltec", "sdkconfig.defaults;sdkconfig.defaults.heltec"),
    "t3s3": ("ripetitore", "build-t3s3", "sdkconfig.defaults;sdkconfig.defaults.t3s3"),
}
# La tastiera del T-Deck (ESP32-C3) e' un progetto di PlatformIO (Arduino): accanto al firmware compilato
# strumenti/tastiera_c3/scrivi_flasher_args.py scrive un flasher_args.json come quello di ESP-IDF.
TASTIERA = FW / "strumenti" / "tastiera_c3"
TASTIERA_BUILD = TASTIERA / ".pio" / "build" / "tastiera"
TASTIERA_INO = TASTIERA / "Keyboard_ESP32C3_Pippaphone" / "Keyboard_ESP32C3_Pippaphone.ino"
ESP32_C3 = 5   # il numero del chip nell'intestazione delle immagini (esp_chip_id_t)


def compila(progetto, cartella, defaults):
    idf = os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("ESP-IDF non e' attivo (manca IDF_PATH): attivalo, oppure usa --solo-pagina.")
    cmd = [sys.executable, str(Path(idf) / "tools" / "idf.py"), "-B", cartella, "-D", f"SDKCONFIG={cartella}/sdkconfig",
           "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build"]
    print(f"== {progetto}: {' '.join(cmd[2:])}", flush=True)
    subprocess.run(cmd, cwd=FW / progetto, check=True)


def pio():
    """Il comando di PlatformIO: nel PATH, o dove lo mette il suo installatore (anche l'estensione di VS Code)."""
    for nome in ("pio", "platformio"):
        if shutil.which(nome):
            return shutil.which(nome)
    casa = Path(os.environ.get("PLATFORMIO_CORE_DIR") or Path.home() / ".platformio")
    for p in (casa / "penv" / "Scripts" / "pio.exe", casa / "penv" / "bin" / "pio"):
        if p.is_file():
            return str(p)
    return None


def compila_tastiera():
    """La tastiera si compila con PlatformIO; senza, o se non riesce, la pagina si fa senza la tastiera."""
    comando = pio()
    if not comando:
        print("== tastiera: PlatformIO non c'e' (strumenti/tastiera_c3/LEGGIMI.md): resta fuori dalla pagina")
        return
    # Senza firmware.bin PlatformIO lo rifa' dal programma gia' compilato e scrivi_flasher_args.py riscrive
    # flasher_args.json: cosi' c'e' anche dopo una compilazione fatta prima di quello script.
    for vecchio in ("firmware.bin", "flasher_args.json"):
        (TASTIERA_BUILD / vecchio).unlink(missing_ok=True)
    print("== tastiera: pio run -e tastiera", flush=True)
    if subprocess.run([comando, "run", "-d", str(TASTIERA), "-e", "tastiera"]).returncode:
        print("   la compilazione della tastiera non e' riuscita: resta fuori dalla pagina")


def descrizione(app):
    """esp_app_desc_t: subito dopo l'intestazione dell'immagine (24 byte) e del primo segmento (8)."""
    if len(app) < 176 or int.from_bytes(app[32:36], "little") != 0xABCD5432:
        return {}
    testo = lambda a, b: app[a:b].split(b"\0")[0].decode("ascii", "replace")
    return {"versione": testo(48, 80), "progetto": testo(80, 112), "ora": testo(112, 128),
            "data": testo(128, 144), "idf": testo(144, 176)}


def firmware(d):
    args_file = d / "flasher_args.json"
    if not args_file.exists():
        print(f"   manca {args_file.relative_to(FW)}: questo dispositivo resta fuori dalla pagina")
        return None
    args = json.loads(args_file.read_text(encoding="utf-8"))
    file = []
    for indirizzo, rel in sorted(args["flash_files"].items(), key=lambda kv: int(kv[0], 16)):
        dati = (d / rel).read_bytes()
        file.append({"indirizzo": int(indirizzo, 16), "nome": Path(rel).name, "dimensione": len(dati),
                     "dati": base64.b64encode(dati).decode("ascii")})
    app = d / args["app"]["file"]
    info = descrizione(app.read_bytes())
    # La data nella descrizione dell'app e' quella dell'ultima compilazione completa: meglio l'ora del file.
    compilato = datetime.datetime.fromtimestamp(app.stat().st_mtime).strftime("%d/%m/%Y alle %H:%M")
    info.update({"compilato": compilato, "flash": args["flash_settings"]["flash_size"], "file": file,
                 "app": Path(args["app"]["file"]).name})
    return info


def tabella_app(tabella):
    """Gli indirizzi delle partizioni app (tipo 0) di una tabella delle partizioni: voci da 32 byte, AA 50."""
    app = set()
    for i in range(0, len(tabella) - 31, 32):
        voce = tabella[i:i + 32]
        if voce[:2] != b"\xaa\x50":
            break
        if voce[2] == 0:
            app.add(int.from_bytes(voce[4:8], "little"))
    return app


def tastiera():
    """Il firmware della tastiera, se c'e' ed e' completo: bootloader a 0, tabella delle partizioni a 0x8000 e
    il programma nella prima partizione app, tutti per l'ESP32-C3. La versione e' quella dello sketch."""
    fw = firmware(TASTIERA_BUILD)
    if not fw:
        return None
    file = {f["indirizzo"]: base64.b64decode(f["dati"]) for f in fw["file"]}
    app = next((f["indirizzo"] for f in fw["file"] if f["nome"] == fw["app"]), None)
    immagine = lambda dati: dati[:1] == b"\xe9" and int.from_bytes(dati[12:14], "little") == ESP32_C3
    controlli = [
        (immagine(file.get(0, b"")), "il bootloader a 0x0 manca o non e' per l'ESP32-C3"),
        (file.get(0x8000, b"")[:2] == b"\xaa\x50", "manca la tabella delle partizioni a 0x8000"),
        (app is not None and immagine(file[app]), "il programma non e' un'immagine per l'ESP32-C3"),
        (app in tabella_app(file.get(0x8000, b"")), "il programma non e' all'inizio di una partizione app"),
    ]
    problema = next((testo for va, testo in controlli if not va), "")
    if problema:
        print(f"   ATTENZIONE, {TASTIERA_BUILD.relative_to(FW)}: {problema}. La tastiera resta fuori dalla pagina.")
        return None
    m = re.search(r"PIPPA_VERSIONE\s*=\s*(\d+)", TASTIERA_INO.read_text(encoding="utf-8"))
    fw["versione"] = f"versione {m.group(1)}" if m else "?"
    return fw


def esptool_js():
    """Il bundle di esptool-js finisce con "export{...}": si mette in window.esptool."""
    src = ESPTOOL_JS.read_text(encoding="utf-8")
    m = re.search(r"export\s*\{([^}]*)\}\s*;?\s*$", src)
    if not m:
        sys.exit(f"{ESPTOOL_JS.name}: non trovo l'export finale")
    coppie = []
    for voce in m.group(1).split(","):
        locale, _, nome = voce.strip().partition(" as ")
        coppie.append(f"{(nome or locale).strip()}:{locale.strip()}")
    js = src[: m.start()] + "window.esptool={" + ",".join(coppie) + "};"
    if "</script" in js.lower():
        sys.exit("esptool-js contiene </script>: non si puo' mettere nella pagina cosi'")
    return js


def maplibre(nome, vietati):
    """Un file di MapLibre da mettere cosi' com'e' dentro uno <script> o uno <style> della pagina."""
    f = MAPLIBRE / nome
    if not f.is_file():
        sys.exit(f"manca {f.relative_to(FW)}: scaricalo da https://unpkg.com/maplibre-gl@4.7.1/dist/{nome}")
    testo = f.read_text(encoding="utf-8")
    for v in vietati:
        if v in testo.lower():
            sys.exit(f"{nome} contiene {v}: non si puo' mettere nella pagina cosi'")
    # Il rimando alla mappa dei sorgenti dentro la pagina non serve (il browser la cercherebbe accanto).
    return re.sub(r"\s*(//# sourceMappingURL=\S+|/\*# sourceMappingURL=[^*]*\*/)\s*$", "\n", testo)


def git_descrizione():
    try:
        return subprocess.run(["git", "describe", "--always", "--dirty"], cwd=FW, capture_output=True,
                              text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--solo-pagina", action="store_true", help="non compila: usa le compilazioni che ci sono")
    a = ap.parse_args()

    if not a.solo_pagina:
        for progetto, cartella, defaults in DISPOSITIVI.values():
            compila(progetto, cartella, defaults)
        compila_tastiera()

    dati = {"creato": datetime.datetime.now().strftime("%d/%m/%Y alle %H:%M"), "git": git_descrizione()}
    pronti = {nome: firmware(FW / progetto / cartella) for nome, (progetto, cartella, _) in DISPOSITIVI.items()}
    pronti["tastiera"] = tastiera()
    for nome, fw in pronti.items():
        if fw:
            dati[nome] = fw
            kb = sum(f["dimensione"] for f in fw["file"]) // 1024
            print(f"   {nome}: {fw.get('versione', '?')}, compilato il {fw['compilato']}, {kb} kB, flash {fw['flash']}")

    pagina = (QUI / "flasher.html").read_text(encoding="utf-8")
    sostituzioni = {
        "/*FIRMWARE*/": "window.FIRMWARE=" + json.dumps(dati, separators=(",", ":")) + ";",
        "/*ESPTOOL*/": esptool_js(),
        "/*MAPLIBRE_CSS*/": maplibre("maplibre-gl.css", ("</style",)),
        # "<!--" in uno script cambia il modo in cui il browser cerca la sua fine: meglio non averlo.
        "/*MAPLIBRE_JS*/": maplibre("maplibre-gl.js", ("</script", "<!--")),
    }
    for segnaposto in sostituzioni:
        if pagina.count(segnaposto) != 1:
            sys.exit(f"flasher.html: il segnaposto {segnaposto} deve esserci una volta")
    # Una passata sola: quello che si mette dentro non viene riletto in cerca di altri segnaposto.
    pagina = re.sub("|".join(map(re.escape, sostituzioni)), lambda m: sostituzioni[m.group(0)], pagina)
    kb = sum(len(sostituzioni[s]) for s in ("/*MAPLIBRE_JS*/", "/*MAPLIBRE_CSS*/")) // 1024
    print(f"   mappe: MapLibre GL JS, {kb} kB")
    USCITA.parent.mkdir(parents=True, exist_ok=True)
    USCITA.write_text(pagina, encoding="utf-8", newline="\n")
    print(f"Pagina pronta: {USCITA} ({USCITA.stat().st_size // 1024} kB). Aprila con Chrome o Edge.")


if __name__ == "__main__":
    main()
