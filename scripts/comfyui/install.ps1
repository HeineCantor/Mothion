<#
Installs ComfyUI (the engine Mothion's "refine a sketch" works with) in a folder of its own, with a Python of its own:
nothing is installed on the system, and removing the folder removes everything.

  scripts\comfyui\install.ps1 [-Dir FOLDER] [-Cuda cu128|cu126|cpu] [-Models] [-Python 3.12]

  -Dir     where to install (default: %LOCALAPPDATA%\Mothion\ComfyUI)
  -Cuda    the PyTorch build: cu128 is for every NVIDIA card, the RTX 50 series included (default when an NVIDIA card
           is found); cpu works, slowly, with no card
  -Models  also download the checkpoint and the ControlNet of the first workflow (about 8.5 GB: the checkpoint, the ControlNet and the tagger)
  -Python  the Python version for ComfyUI's own environment (default 3.12; uv fetches it)
  -Stamp   what to write in <folder>\.mothion-install when everything is done (Mothion asks for it, and takes the
           install as complete only with its number there)
#>
param(
    [string]$Dir = (Join-Path $env:LOCALAPPDATA 'Mothion\ComfyUI'),
    [string]$Cuda = '',
    [switch]$Models,
    [string]$Python = '3.12',
    [string]$Stamp = '1'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue' # Invoke-WebRequest is far faster without its progress bar

function Say($text) { Write-Host "`n==> $text" }
function Run($file, [string[]]$arguments) {
    & $file @arguments
    if ($LASTEXITCODE -ne 0) { throw "$file failed (exit code $LASTEXITCODE)" }
}

$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
New-Item -ItemType Directory -Force -Path $Dir | Out-Null
$Dir = (Resolve-Path $Dir).Path

if (-not $Cuda) {
    $Cuda = 'cpu'
    if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
        & nvidia-smi -L *> $null
        if ($LASTEXITCODE -eq 0) { $Cuda = 'cu128' }
    }
}
Write-Host "Installing in $Dir (PyTorch: $Cuda)"

# --- uv: installs Python and the packages, fast, in the folder -----------------------------------------------------------
$uvCommand = Get-Command uv -ErrorAction SilentlyContinue
if ($uvCommand) {
    $Uv = $uvCommand.Source
} else {
    Say 'Getting uv'
    $arch = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'aarch64' } else { 'x86_64' }
    $zip = Join-Path $Dir 'uv.zip'
    Invoke-WebRequest "https://github.com/astral-sh/uv/releases/latest/download/uv-$arch-pc-windows-msvc.zip" -OutFile $zip
    Expand-Archive -Force $zip (Join-Path $Dir '.uv')
    Remove-Item $zip
    $Uv = Join-Path $Dir '.uv\uv.exe'
}

# --- ComfyUI itself ---------------------------------------------------------------------------------------------------------
$Src = Join-Path $Dir 'ComfyUI'
if (Test-Path (Join-Path $Src '.git')) {
    Say 'Updating ComfyUI'
    Run git @('-C', $Src, 'pull', '--ff-only')
} elseif (Get-Command git -ErrorAction SilentlyContinue) {
    Say 'Downloading ComfyUI'
    if ((Test-Path $Src) -and (Get-ChildItem $Src -Force | Select-Object -First 1)) {
        # Something is in the folder already (models put there beforehand, an install that was interrupted): ComfyUI goes
        # in around it, and what is there is kept.
        Run git @('-C', $Src, 'init', '-q')
        Run git @('-C', $Src, 'remote', 'add', 'origin', 'https://github.com/comfyanonymous/ComfyUI.git')
        Run git @('-C', $Src, 'fetch', '-q', '--depth', '1', 'origin', 'HEAD')
        Run git @('-C', $Src, 'checkout', '-q', '-f', '-B', 'master', 'FETCH_HEAD')
    } else {
        Run git @('clone', '--depth', '1', 'https://github.com/comfyanonymous/ComfyUI.git', $Src)
    }
} else {
    Say 'Downloading ComfyUI (no git: as an archive)'
    $zip = Join-Path $Dir 'comfyui.zip'
    Invoke-WebRequest 'https://github.com/comfyanonymous/ComfyUI/archive/refs/heads/master.zip' -OutFile $zip
    Expand-Archive -Force $zip $Dir
    Move-Item (Join-Path $Dir 'ComfyUI-master') $Src
    Remove-Item $zip
}

# --- its Python and packages -------------------------------------------------------------------------------------------------
Say "Python $Python environment"
Run $Uv @('venv', '--python', $Python, '--allow-existing', (Join-Path $Dir '.venv'))
$Py = Join-Path $Dir '.venv\Scripts\python.exe'

Say "PyTorch ($Cuda)"
Run $Uv @('pip', 'install', '--python', $Py, 'torch', 'torchvision', 'torchaudio', '--index-url', "https://download.pytorch.org/whl/$Cuda")

Say "ComfyUI's packages"
Run $Uv @('pip', 'install', '--python', $Py, '-r', (Join-Path $Src 'requirements.txt'))

# --- the tagger: Mothion's "Suggest" asks it for the tags of a picture -----------------------------------------------------------
Say 'Tagger (custom node)'
$Node = Join-Path $Src 'custom_nodes\ComfyUI-WD14-Tagger'
if (Test-Path (Join-Path $Node '.git')) {
    & git -C $Node pull --ff-only
} elseif (-not (Test-Path (Join-Path $Node '__init__.py'))) {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        Run git @('clone', '--depth', '1', 'https://github.com/pythongosssss/ComfyUI-WD14-Tagger.git', $Node)
    } else {
        $zip = Join-Path $Dir 'tagger.zip'
        Invoke-WebRequest 'https://github.com/pythongosssss/ComfyUI-WD14-Tagger/archive/refs/heads/main.zip' -OutFile $zip
        Expand-Archive -Force $zip (Join-Path $Src 'custom_nodes')
        Move-Item (Join-Path $Src 'custom_nodes\ComfyUI-WD14-Tagger-main') $Node
        Remove-Item $zip
    }
}
Run $Uv @('pip', 'install', '--python', $Py, 'onnxruntime')

# --- Mothion's workflows, and a launcher ----------------------------------------------------------------------------------------
Say 'Workflows and launcher'
$Own = Join-Path $Dir 'Mothion'
New-Item -ItemType Directory -Force -Path $Own | Out-Null
Copy-Item (Join-Path $Here 'workflows\*.json') $Own -Force
$launcher = @(
    '@echo off',
    'rem Starts ComfyUI for Mothion: the address is http://127.0.0.1:8188 (only this computer can reach it).',
    "cd /d `"$Src`"",
    "`"$Py`" main.py --listen 127.0.0.1 --port 8188 %*"
)
Set-Content -Path (Join-Path $Dir 'start.bat') -Value $launcher -Encoding ASCII

# --- the models (only if asked) ---------------------------------------------------------------------------------------------------
function Fetch($url, $folder, $file) {
    $target = Join-Path $Src "models\$folder"
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    $path = Join-Path $target $file
    if (Test-Path $path) { Write-Host "$file is there already"; return }
    Write-Host "Downloading $file"
    Run curl.exe @('-fL', '-C', '-', '--progress-bar', '-o', "$path.part", $url) # resumes if it was interrupted
    Move-Item "$path.part" $path
}
if ($Models) {
    Say 'Models (read their licenses on the pages they come from)'
    Fetch 'https://huggingface.co/Laxhar/noobai-XL-1.1/resolve/main/NoobAI-XL-v1.1.safetensors' 'checkpoints' 'NoobAI-XL-v1.1.safetensors'
    Fetch 'https://huggingface.co/TheMistoAI/MistoLine/resolve/main/mistoLine_rank256.safetensors' 'controlnet' 'mistoLine_rank256.safetensors'
    # The tagger's model, where the node looks for it (it would download it itself on first use, slowly and unseen).
    $taggerModels = Join-Path $Node 'models'
    New-Item -ItemType Directory -Force -Path $taggerModels | Out-Null
    foreach ($pair in @(@('model.onnx', 'wd-swinv2-tagger-v3.onnx'), @('selected_tags.csv', 'wd-swinv2-tagger-v3.csv'))) {
        $target = Join-Path $taggerModels $pair[1]
        if (-not (Test-Path $target)) {
            Write-Host "Downloading $($pair[1])"
            Run curl.exe @('-fL', '-C', '-', '--progress-bar', '-o', "$target.part", "https://huggingface.co/SmilingWolf/wd-swinv2-tagger-v3/resolve/main/$($pair[0])")
            Move-Item "$target.part" $target
        }
    }
}

# --- done: the number Mothion asked for says so -----------------------------------------------------------------------------------
Set-Content -Path (Join-Path $Dir '.mothion-install') -Value $Stamp -Encoding ASCII

Write-Host @"

Done.

  Start ComfyUI:   $Dir\start.bat
  Then open:       http://127.0.0.1:8188   and drag $Own\mothion_sketch_refine.json onto the page.

  The models go in:
    $Src\models\checkpoints\   an anime checkpoint (NoobAI-XL-v1.1.safetensors, or Illustrious-XL-v1.0.safetensors)
    $Src\models\controlnet\    mistoLine_rank256.safetensors
  (scripts\comfyui\README.md says where to get them; install.ps1 -Models downloads those two.)
"@
