#!/bin/sh
# Pianificatore radio PippaNet: avvia il server locale e apre il browser (macOS, anche Linux).
#   sh strumenti/pianificatore/avvia.sh
# Ctrl+C per fermarlo. La cache dei dati va in ~/.cache/pippaphone-pianificatore.
cd "$(dirname "$0")" || exit 1
if ! command -v node >/dev/null 2>&1; then
  echo "Serve Node.js (versione 18 o piu' nuova): https://nodejs.org oppure 'brew install node'"
  exit 1
fi
(
  sleep 2
  if command -v open >/dev/null 2>&1; then open "http://localhost:8787"
  elif command -v xdg-open >/dev/null 2>&1; then xdg-open "http://localhost:8787"
  fi
) &
exec node server.js
