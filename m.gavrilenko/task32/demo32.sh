#!/bin/sh
# demo32.sh : async и sync сервера работают ОДНОВРЕМЕННО, печатают в этот же tty.
#   ./demo32.sh                     живое демо (3 клиента x N сообщений)
#   ./demo32.sh bench [CNT] [LEN]   замер скорости (сообщений не печатает)
MODE=${1:-live}
CNT=${2:-8}; LO=${3:-40}; HI=${4:-300}; LEN=${5:-14}
if [ "$MODE" = bench ]; then CNT=${2:-5000}; LO=0; HI=0; LEN=${3:-200}; fi

A=/tmp/upper32_a.sock; S=/tmp/upper32_s.sock
pkill -x server32 2>/dev/null; pkill -x client32 2>/dev/null
rm -f $A $S async.log sync.log

# при любом выходе (в т.ч. по ошибке или Ctrl-C) не оставляем висящих серверов
trap 'pkill -f "server32 async" 2>/dev/null; pkill -f "server32 sync" 2>/dev/null' EXIT INT TERM
sleep 1

if [ "$MODE" = bench ]; then
    ./server32 async bench >async.log 2>&1 &
    ./server32 sync  bench >sync.log  2>&1 &
else
    ./server32 async 2>&1 | tee async.log &
    ./server32 sync  2>&1 | tee sync.log  &
fi
sleep 1

# проверка: сервер жив И понял свой режим (wc -l на Solaris печатает с полями,
# строковое сравнение с "2" всегда ложно, поэтому смотрим в логи)
if ! grep -q 'Server 32 \[ASYNC\]' async.log || ! grep -q 'Server 32 \[SYNC' sync.log; then
    echo "ERR: сервер не поднялся (или не понял async/sync):"
    sed 's/^/  A| /' async.log 2>/dev/null
    sed 's/^/  S| /' sync.log  2>/dev/null
    exit 1
fi

# --- клиенты: собираем PID'ы, ждём ТОЛЬКО их (не серверы!) ---
CL=""
for n in ALPHA BRAVO DELTA; do
    UPPER_SOCK=$A ./client32 $n $CNT $LO $HI $LEN & CL="$CL $!"
    UPPER_SOCK=$S ./client32 $n $CNT $LO $HI $LEN & CL="$CL $!"
done
T0=$(date +%s)
for p in $CL; do wait $p; done
T1=$(date +%s)
sleep 0.5

# --- серверов гасим ПО ОЧЕРЕДИ, иначе два ИТОГ напечатаются вперемешку ---
pkill -f 'server32 async'; sleep 1
pkill -f 'server32 sync';  sleep 1

# ---------- 1) кто кого ждал: моменты ПРИЁМА клиентов ----------
echo
echo "Моменты, когда сервер принял клиента (t от старта):"
awk '
$1=="t=" && index($0,"(") && index($0,"fd=") && !index($0,"|") {
    m = ($3=="ASYNC") ? "A" : "S"; s[m] = s[m] " " $2; c[m]++ }
END { printf "  ASYNC:%s\n", s["A"]; printf "  SYNC :%s\n", s["S"] }' async.log sync.log

# ---------- 2) окна обслуживания каждого клиента ----------
echo
echo "Okna obsluzhivaniya klienta (pervoe -> poslednee soobshchenie):"
awk '
$1=="t=" && index($0,"|") {
    nm = ""
    for (i=1; i<=NF; i++) if (index($i,"#")) { p=index($i,"#"); nm=substr($i,1,p-1); break }
    if (nm == "") next
    m = ($3=="ASYNC") ? "A" : "S"; k = m nm
    if (!(k in f)) f[k]=$2
    l[k]=$2; n[k]++
    w[m] = (w[m]=="" || $2+0 > w[m]+0) ? $2 : w[m]
    if (s[m]=="" || $2+0 < s[m]+0) s[m]=$2 }
END { split("ALPHA BRAVO DELTA", q, " ")
    printf "  %-6s %-26s %s\n", "client", "ASYNC", "SYNC"
    for (i=1; i<=3; i++) { c=q[i]
        printf "  %-6s %8s ->%8s (%3d)  %8s ->%8s (%3d)\n", c,
               f["A" c], l["A" c], n["A" c], f["S" c], l["S" c], n["S" c] }
    printf "  pervyi -> poslednyi bait:  ASYNC %s -> %s     SYNC %s -> %s\n",
           s["A"], w["A"], s["S"], w["S"] }' async.log sync.log

# ---------- 3) сводка и сравнение времени ----------
echo "======== SRAVNENIE (wall: $((T1 - T0)) s) ========"
grep -h '^#SUMMARY' async.log sync.log | nawk '
{ t[$2]=$3; m[$2]=$4; b[$2]=$5 }
END { printf "%-6s %10s %10s %11s %9s\n", "rezhim", "priem_s", "soobshch", "soobsh_s", "KB_s"
      n = split("ASYNC SYNC", o, " ")
      for (i=1; i<=n; i++) { q=o[i]
        if (q in t) printf "%-6s %10.3f %10d %11.1f %9.1f\n", q, t[q], m[q],
               (t[q]>0 ? m[q]/t[q] : 0), (t[q]>0 ? b[q]/t[q]/1024 : 0) }
      if (t["ASYNC"] > 0 && t["SYNC"] > 0)
          printf "\n=> SYNC DOLHE V %.2f RAZA (soobshchenii odinakovo: %d)\n",
                 t["SYNC"]/t["ASYNC"], m["ASYNC"]
      if (m["ASYNC"] == 0 || m["SYNC"] == 0)
          print "!! 0 soobshchenii: klientsy ne podklyuchilis" }'
