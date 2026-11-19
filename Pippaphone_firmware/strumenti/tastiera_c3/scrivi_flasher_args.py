"""Script di PlatformIO (extra_scripts in platformio.ini) per la pagina che carica il firmware dal browser.

Dopo la compilazione scrive .pio/build/tastiera/flasher_args.json, come quello di ESP-IDF: i file da scrivere coi
loro indirizzi e le impostazioni della flash. I file sono gli stessi che scriverebbe "pio run -t upload"
(bootloader, tabella delle partizioni, boot_app0 e il programma) e finiscono tutti nella cartella della
compilazione. Lo legge strumenti/flasher/prepara.py.
"""
import json
import os
import shutil

Import("env")  # noqa: F821 (lo mette PlatformIO)


def scrivi(source, target, env):
    cartella = env.subst("$BUILD_DIR")
    programma = target[0].get_abspath()
    scheda = env.BoardConfig()
    # Le immagini in piu' sono quelle che PlatformIO da' a esptool prima del programma.
    immagini = [(indirizzo, env.subst(percorso)) for indirizzo, percorso in env.get("FLASH_EXTRA_IMAGES", [])]
    immagini.append((env.subst("$ESP32_APP_OFFSET") or "0x10000", programma))
    file = {}
    for indirizzo, percorso in immagini:
        nome = os.path.basename(percorso)
        copia = os.path.join(cartella, nome)
        if os.path.normcase(os.path.abspath(percorso)) != os.path.normcase(os.path.abspath(copia)):
            shutil.copyfile(percorso, copia)
        file["0x%x" % int(env.subst(indirizzo), 0)] = nome
    args = {
        "flash_settings": {
            "flash_mode": env.subst("${__get_board_flash_mode(__env__)}") or scheda.get("build.flash_mode", ""),
            "flash_freq": env.subst("${__get_board_f_flash(__env__)}"),
            "flash_size": scheda.get("upload.flash_size", "4MB"),
        },
        "flash_files": file,
        "app": {"file": os.path.basename(programma)},
        "extra_esptool_args": {"chip": scheda.get("build.mcu", "")},
    }
    with open(os.path.join(cartella, "flasher_args.json"), "w", encoding="utf-8") as f:
        json.dump(args, f, indent=4)
        f.write("\n")
    print("flasher_args.json: " + ", ".join(f"{nome} a {indirizzo}" for indirizzo, nome in
                                           sorted(file.items(), key=lambda kv: int(kv[0], 16))))


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", scrivi)  # noqa: F821
