#!/usr/bin/env python3
"""Prepara la pagina per caricare il firmware dal browser.

Compila il telefono per il LilyGO T-Deck Plus e il ripetitore per Heltec V3 e LilyGO T3-S3, poi
mette i firmware e esptool-js dentro il modello flasher.html e scrive una pagina sola:

    build-flasher/pippaphone_flasher.html

che si apre con Chrome o Edge anche con un doppio clic, senza server e senza internet.

Uso, dalla cartella Pippaphone_firmware:
    python strumenti/flasher/prepara.py               compila (serve ESP-IDF attivo) e prepara la pagina
    python strumenti/flasher/prepara.py --solo-pagina  prepara la pagina con le compilazioni che ci sono
"""
import argparse
import base64
import datetime
import json
import os
import re
import subprocess
import sys
from pathlib import Path

QUI = Path(__file__).resolve().parent
FW = QUI.parent.parent
ESPTOOL_JS = QUI / "esptool-js" / "esptool-js-0.7.0.js"
USCITA = FW / "build-flasher" / "pippaphone_flasher.html"

# dispositivo -> (progetto, cartella di compilazione, file sdkconfig.defaults)
DISPOSITIVI = {
    "tdeck": ("telefono", "build-tdeck", "sdkconfig.defaults;sdkconfig.defaults.tdeck"),
    "heltec": ("ripetitore", "build-heltec", "sdkconfig.defaults;sdkconfig.defaults.heltec"),
    "t3s3": ("ripetitore", "build-t3s3", "sdkconfig.defaults;sdkconfig.defaults.t3s3"),
}


def compila(progetto, cartella, defaults):
    idf = os.environ.get("IDF_PATH")
    if not idf:
        sys.exit("ESP-IDF non e' attivo (manca IDF_PATH): attivalo, oppure usa --solo-pagina.")
    cmd = [sys.executable, str(Path(idf) / "tools" / "idf.py"), "-B", cartella, "-D", f"SDKCONFIG={cartella}/sdkconfig",
           "-D", f"SDKCONFIG_DEFAULTS={defaults}", "build"]
    print(f"== {progetto}: {' '.join(cmd[2:])}", flush=True)
    subprocess.run(cmd, cwd=FW / progetto, check=True)


def descrizione(app):
    """esp_app_desc_t: subito dopo l'intestazione dell'immagine (24 byte) e del primo segmento (8)."""
    if len(app) < 176 or int.from_bytes(app[32:36], "little") != 0xABCD5432:
        return {}
    testo = lambda a, b: app[a:b].split(b"\0")[0].decode("ascii", "replace")
    return {"versione": testo(48, 80), "progetto": testo(80, 112), "ora": testo(112, 128),
            "data": testo(128, 144), "idf": testo(144, 176)}


def firmware(progetto, cartella):
    d = FW / progetto / cartella
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
    info.update({"compilato": compilato, "flash": args["flash_settings"]["flash_size"], "file": file})
    return info


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

    dati = {"creato": datetime.datetime.now().strftime("%d/%m/%Y alle %H:%M"), "git": git_descrizione()}
    for nome, (progetto, cartella, _) in DISPOSITIVI.items():
        fw = firmware(progetto, cartella)
        if fw:
            dati[nome] = fw
            kb = sum(f["dimensione"] for f in fw["file"]) // 1024
            print(f"   {nome}: {fw.get('versione', '?')}, compilato il {fw['compilato']}, {kb} kB, flash {fw['flash']}")

    pagina = (QUI / "flasher.html").read_text(encoding="utf-8")
    for segnaposto, testo in (("/*FIRMWARE*/", "window.FIRMWARE=" + json.dumps(dati, separators=(",", ":")) + ";"),
                              ("/*ESPTOOL*/", esptool_js())):
        if pagina.count(segnaposto) != 1:
            sys.exit(f"flasher.html: il segnaposto {segnaposto} deve esserci una volta")
        pagina = pagina.replace(segnaposto, testo)
    USCITA.parent.mkdir(parents=True, exist_ok=True)
    USCITA.write_text(pagina, encoding="utf-8", newline="\n")
    print(f"Pagina pronta: {USCITA} ({USCITA.stat().st_size // 1024} kB). Aprila con Chrome o Edge.")


if __name__ == "__main__":
    main()
