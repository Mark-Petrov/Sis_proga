#!/bin/bash
# Простая демонстрация курсовой СУБД
#
# Использование:
#   1) Терминал 1: ./build/db_storage 4100 data
#   2) Терминал 2: ./demo.sh
#
# Если порт занят: lsof -ti :4100 | xargs kill

set -e
cd "$(dirname "$0")"

HOST="${STORAGE_HOST:-127.0.0.1}"
PORT="${STORAGE_PORT:-4100}"
BUILD="./build"

if [[ ! -x "$BUILD/prog" ]]; then
  echo "Сначала соберите проект:"
  echo "  rm -rf build && mkdir build && cd build"
  echo "  cmake -DCMAKE_OSX_ARCHITECTURES=arm64 -DOPENSSL_ROOT_DIR=/opt/homebrew/opt/openssl@3 .."
  echo "  cmake --build ."
  exit 1
fi

if ! nc -z "$HOST" "$PORT" 2>/dev/null; then
  echo "Сервер не отвечает на $HOST:$PORT"
  echo "Запустите в другом терминале:"
  echo "  ./build/db_storage $PORT data"
  exit 1
fi

echo "=== 1. Пакетный SQL (demo.sql) ==="
export STORAGE_HOST="$HOST"
export STORAGE_PORT="$PORT"
"$BUILD/prog" demo.sql

echo ""
echo "=== 2. Телеметрия (METRICS) ==="
printf 'METRICS\n' | nc "$HOST" "$PORT" || true

echo ""
echo "=== 3. Асинхронный запрос (JOB + POLL) ==="
RESP=$(printf '1\t\tUSE demo;\n' | nc "$HOST" "$PORT")
echo "$RESP"
JOB_ID="${RESP#JOB	}"
if [[ "$RESP" == JOB* ]] && [[ -n "$JOB_ID" ]]; then
  sleep 0.3
  printf 'POLL\t%s\n' "$JOB_ID" | nc "$HOST" "$PORT"
fi

echo ""
echo "=== 4. Журнал доступа ==="
if [[ -f data/access.log ]]; then
  tail -5 data/access.log
else
  echo "(файл data/access.log пока не создан)"
fi

echo ""
echo "=== 5. Журнал мутаций ==="
if [[ -f data/demo/journal.log ]]; then
  tail -5 data/demo/journal.log
else
  echo "(файл data/demo/journal.log пока не создан)"
fi

echo ""
echo "Готово. Интерактивный режим: ./build/prog"
