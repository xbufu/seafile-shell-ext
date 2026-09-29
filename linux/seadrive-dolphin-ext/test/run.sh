# usage: run.sh <harness-build-dir> <installed Qt plugins dir> <KF major version>
set -e
H=$(mktemp -d); mkdir -p $H/SeaDrive/lib $H/.seadrive; touch $H/SeaDrive/lib/a.txt
echo '{"status":"synced","cached":"cached"}' > $H/state.json
python3 $(dirname $0)/fake_seadrive.py $H & PID=$!; sleep 0.5
HOME=$H QT_PLUGIN_PATH=$2 QT_QPA_PLATFORM=offscreen $1/harness $2/kf$3/overlayicon/libseadrive-dolphin-overlay.so; RC=$?
kill $PID; exit $RC
