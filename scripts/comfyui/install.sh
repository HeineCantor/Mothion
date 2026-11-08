#!/usr/bin/env bash
# Installs ComfyUI (the engine Mothion's "refine a sketch" works with) in a folder of its own, with a Python of its own:
# nothing is installed on the system, and removing the folder removes everything.
#
#   scripts/comfyui/install.sh [--dir FOLDER] [--cuda cu128|cu126|cpu] [--models] [--python 3.12]
#
#   --dir      where to install (default: ~/.local/share/Mothion/ComfyUI)
#   --cuda     the PyTorch build: cu128 is for every NVIDIA card, the RTX 50 series included (default when an NVIDIA
#              card is found); cpu works, slowly, with no card
#   --models   also download the checkpoint and the ControlNet of the first workflow (about 8 GB)
#   --python   the Python version for ComfyUI's own environment (default 3.12; uv fetches it if the system has none)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DIR="${XDG_DATA_HOME:-$HOME/.local/share}/Mothion/ComfyUI"
CUDA=""
MODELS=0
PYTHON_VERSION="3.12"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --dir) DIR="$2"; shift 2 ;;
        --cuda) CUDA="$2"; shift 2 ;;
        --models) MODELS=1; shift ;;
        --python) PYTHON_VERSION="$2"; shift 2 ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
done

say() { printf '\n==> %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "'$1' is needed and was not found." >&2; exit 1; }; }

need curl
mkdir -p "$DIR"
DIR="$(cd "$DIR" && pwd)"

if [[ -z "$CUDA" ]]; then
    if command -v nvidia-smi >/dev/null 2>&1 && nvidia-smi -L >/dev/null 2>&1; then CUDA="cu128"; else CUDA="cpu"; fi
fi
echo "Installing in $DIR (PyTorch: $CUDA)"

# --- uv: installs Python and the packages, fast, in the folder ----------------------------------------------------
UV="$(command -v uv || true)"
if [[ -z "$UV" ]]; then
    say "Getting uv"
    case "$(uname -s)-$(uname -m)" in
        Linux-x86_64) TARGET="x86_64-unknown-linux-gnu" ;;
        Linux-aarch64|Linux-arm64) TARGET="aarch64-unknown-linux-gnu" ;;
        Darwin-arm64) TARGET="aarch64-apple-darwin" ;;
        Darwin-x86_64) TARGET="x86_64-apple-darwin" ;;
        *) echo "No uv build known for $(uname -s) $(uname -m): install uv (https://docs.astral.sh/uv/) and run this again." >&2; exit 1 ;;
    esac
    mkdir -p "$DIR/.uv"
    curl -fsSL "https://github.com/astral-sh/uv/releases/latest/download/uv-$TARGET.tar.gz" | tar -xz --strip-components=1 -C "$DIR/.uv"
    UV="$DIR/.uv/uv"
fi

# --- ComfyUI itself -------------------------------------------------------------------------------------------------
SRC="$DIR/ComfyUI"
if [[ -d "$SRC/.git" ]]; then
    say "Updating ComfyUI"
    git -C "$SRC" pull --ff-only
elif command -v git >/dev/null 2>&1; then
    say "Downloading ComfyUI"
    git clone --depth 1 https://github.com/comfyanonymous/ComfyUI.git "$SRC"
else
    say "Downloading ComfyUI (no git: as an archive)"
    mkdir -p "$SRC"
    curl -fsSL https://github.com/comfyanonymous/ComfyUI/archive/refs/heads/master.tar.gz | tar -xz --strip-components=1 -C "$SRC"
fi

# --- its Python and packages ------------------------------------------------------------------------------------------
say "Python $PYTHON_VERSION environment"
"$UV" venv --python "$PYTHON_VERSION" --allow-existing "$DIR/.venv"
PY="$DIR/.venv/bin/python"

say "PyTorch ($CUDA)"
if [[ "$CUDA" == "cpu" ]]; then
    "$UV" pip install --python "$PY" torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
else
    "$UV" pip install --python "$PY" torch torchvision torchaudio --index-url "https://download.pytorch.org/whl/$CUDA"
fi

say "ComfyUI's packages"
"$UV" pip install --python "$PY" -r "$SRC/requirements.txt"

# --- Mothion's workflows, and a launcher --------------------------------------------------------------------------------
say "Workflows and launcher"
mkdir -p "$DIR/Mothion"
cp "$HERE"/workflows/*.json "$DIR/Mothion/"
cat > "$DIR/start.sh" <<LAUNCH
#!/usr/bin/env bash
# Starts ComfyUI for Mothion: the address is http://127.0.0.1:8188 (only this computer can reach it).
cd "$SRC"
exec "$PY" main.py --listen 127.0.0.1 --port 8188 "\$@"
LAUNCH
chmod +x "$DIR/start.sh"

# --- the models (only if asked) -------------------------------------------------------------------------------------------
fetch() { # url, folder, file
    mkdir -p "$SRC/models/$2"
    if [[ -f "$SRC/models/$2/$3" ]]; then echo "$3 is there already"; return; fi
    echo "Downloading $3"
    curl -fL -C - --progress-bar -o "$SRC/models/$2/$3.part" "$1" && mv "$SRC/models/$2/$3.part" "$SRC/models/$2/$3"
}
if [[ "$MODELS" == 1 ]]; then
    say "Models (read their licenses on the pages they come from)"
    fetch "https://huggingface.co/Laxhar/noobai-XL-1.1/resolve/main/NoobAI-XL-v1.1.safetensors" checkpoints NoobAI-XL-v1.1.safetensors
    fetch "https://huggingface.co/TheMistoAI/MistoLine/resolve/main/mistoLine_rank256.safetensors" controlnet mistoLine_rank256.safetensors
fi

cat <<DONE

Done.

  Start ComfyUI:   $DIR/start.sh
  Then open:       http://127.0.0.1:8188   and drag $DIR/Mothion/mothion_sketch_refine.json onto the page.

  The models go in:
    $SRC/models/checkpoints/   an anime checkpoint (NoobAI-XL-v1.1.safetensors, or Illustrious-XL-v1.0.safetensors)
    $SRC/models/controlnet/    mistoLine_rank256.safetensors
  (scripts/comfyui/README.md says where to get them; install.sh --models downloads those two.)
DONE
