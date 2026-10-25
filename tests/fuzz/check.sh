#!/bin/bash
# Differential test of one random program.   usage: check.sh SEED OUTDIR
# gen.py writes a random Leo program plus the output predicted by an
# independent reference interpreter.  The program is built with and without
# the optimizer, and run with the IR interpreter; all three outputs must equal
# the prediction.
seed=$1; d=$2
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LEOC="${LEOC:-$HERE/../../leoc}"
mkdir -p "$d"
python3 "$HERE/gen.py" "$seed" "$d" || { echo "GENFAIL $seed"; exit 0; }
st=$(cat "$d/p$seed.status")
if [ "$st" = skip ]; then echo "SKIP $seed"; exit 0; fi
for mode in opt noopt interp; do
  flag=""; [ $mode = noopt ] && flag="--no-opt"; [ $mode = interp ] && flag="--interp"
  "$LEOC" "$d/p$seed.leo" --run -q $flag -o "$d/p${seed}_$mode" --outdir "$d" < "$d/p$seed.in" > "$d/p$seed.$mode.out" 2> "$d/p$seed.$mode.err"
  rc=$?
  if [ $rc -ge 2 ] && [ $rc -le 6 ]; then echo "FAIL $seed $mode compile rc=$rc: $(head -3 "$d/p$seed.$mode.err" | tr '\n' '|')"; continue; fi
  if ! cmp -s "$d/p$seed.$mode.out" "$d/p$seed.expect"; then echo "FAIL $seed $mode output differs from oracle"; continue; fi
  if [ "$st" = rt ]; then
     if [ $rc != 1 ] || ! grep -q "^runtime error" "$d/p$seed.$mode.err"; then echo "FAIL $seed $mode expected runtime error rc=$rc"; continue; fi
  else
     if [ $rc != 0 ]; then echo "FAIL $seed $mode rc=$rc"; continue; fi
  fi
  echo "PASS $seed $mode $st"
done
