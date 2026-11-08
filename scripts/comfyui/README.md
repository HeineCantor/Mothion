# ComfyUI for Mothion

Mothion's "refine a sketch" (coming) talks to [ComfyUI](https://github.com/comfyanonymous/ComfyUI), which runs the
image model on your own graphics card. These scripts install it in a folder of its own, with a Python of its own
(through [uv](https://docs.astral.sh/uv/), which they fetch if it is not there): nothing is installed on the system,
and deleting the folder removes everything.

## From Mothion

Nothing here has to be run by hand: in Mothion, **Edit > Refine with AI** has a row with **Start** and **Stop** for the
server. If ComfyUI is not installed (or the install is older than the program: the scripts add things over time), Start
asks, then runs the install script that is inside the program (these very files), with the models, and starts the server
when it is done. The row shows the steps and the downloads; Stop during the install stops it, and Start goes on from
where it was (downloads are resumed). Where it installs is in **Settings > AI**.

The rest of this page is for installing by hand, or for what the script does.

## Install

Linux / macOS:

    scripts/comfyui/install.sh              # add --models to also download the models below (about 8.5 GB)

Windows (PowerShell):

    scripts\comfyui\install.ps1             # add -Models to also download them

Options: the folder (`--dir` / `-Dir`; default `~/.local/share/Mothion/ComfyUI` or `%LOCALAPPDATA%\Mothion\ComfyUI`),
the PyTorch build (`--cuda cu128|cu126|cpu`, `-Cuda`; `cu128` is the default with an NVIDIA card, RTX 50 series
included) and the Python version. Running it again updates ComfyUI.

Start it with `start.sh` / `start.bat` in the install folder, then open <http://127.0.0.1:8188> (only this computer
can reach it). Drag `Mothion/mothion_sketch_refine.json` (also in the install folder) onto the page to load the
workflow.

## The models

For anime and manga sketches, with 8 GB of video memory. They go in the `models` folder of ComfyUI:

| What | File | Where it goes | From |
|------|------|---------------|------|
| Checkpoint (anime, SDXL) | `NoobAI-XL-v1.1.safetensors` (7.1 GB) | `models/checkpoints/` | <https://huggingface.co/Laxhar/noobai-XL-1.1> |
| Or another checkpoint | `Illustrious-XL-v1.0.safetensors` (6.9 GB) | `models/checkpoints/` | <https://huggingface.co/OnomaAIResearch/Illustrious-XL-v1.0> |
| ControlNet (keeps the drawing) | `mistoLine_rank256.safetensors` (0.8 GB) | `models/controlnet/` | <https://huggingface.co/TheMistoAI/MistoLine> |
| Tagger (for "Suggest") | `wd-swinv2-tagger-v3.onnx` and `.csv` (0.5 GB) | `custom_nodes/ComfyUI-WD14-Tagger/models/` | <https://huggingface.co/SmilingWolf/wd-swinv2-tagger-v3> (`model.onnx` and `selected_tags.csv`) |

Read the licenses on those pages (the checkpoints have their own terms for commercial use; MistoLine asks for
attribution). If you use Illustrious, pick it in the checkpoint node of the workflow.

## The tagger ("Suggest")

The **Suggest** button of the window asks `workflows/mothion_tag_image.json` what the picture shows: the
[WD14 tagger](https://github.com/pythongosssss/ComfyUI-WD14-Tagger), a custom node the install script adds (with
`onnxruntime`), tells it as booru tags, the vocabulary of the anime checkpoints, and they go into the prompt to be
corrected. The negative cannot be read from a picture: it is the standing one of Settings > AI.

## The workflow

`workflows/mothion_sketch_refine.json` is the minimal one: the sketch goes in, is brought to about one megapixel, and
the model redraws it **starting from the sketch** (img2img) while a ControlNet keeps the lines where they are.

What to play with, in the nodes:

- **MOTHION_PROMPT** (positive) and **MOTHION_NEGATIVE**: tags in booru style, as these models were trained
  (`1girl, long hair, blue eyes, ...`).
- **MOTHION_SAMPLER**, `denoise`: how far it goes from the sketch. 0.4 keeps nearly all of it (clean-up and coloring),
  0.75 is the default, 0.8 and more reinvents. `seed` fixed gives the same picture again; `cfg` 4 to 6 and 28 steps are
  what these models like.
- **The ControlNet node** (`strength`, `end_percent`): how tightly the lines are followed, and at which point of the
  drawing the model is let loose. Raise the strength if lines wander; lower `end_percent` for more freedom.
- The **Invert Image** node turns black lines on white into the white lines on black that the ControlNet reads. If the
  result ignores your lines, try bypassing it (select it, Ctrl+B).
- A **LoRA** for a character or a style: add a "Load LoRA" node after the checkpoint (its `models/loras/` folder) and
  wire the model and clip through it.

Node titles starting with `MOTHION_` are the ones Mothion will find and fill in (the sketch, the prompt, the seed, the
denoise, the result), so keep them if you change the graph.

## If it runs out of video memory

ComfyUI manages memory by itself and an 8 GB card runs these models; if it still fails, start it with
`start.sh --lowvram` (slower), and close what else uses the card.
