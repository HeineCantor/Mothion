"""Mothion Link: gives Mothion (the animation program) the frames of the scene as the 3D viewport shows them, live.

Mothion listens on the local computer; this add-on connects to it (and tries again until Mothion is there), tells it
which scene is open and when it changes, and draws the frames it asks for off screen from the scene's active camera.
It only reads the scene: the frames are drawn on a hidden copy of it (which shares the objects, so every edit is seen),
so the timeline never moves while you work, and the viewport's shading, overlays and compositor are put back as they
were after every request. Nothing is sent outside this computer.

The messages are binary frames: two little-endian 32-bit lengths, a JSON header, then a payload (the pixels of a frame).
"""

bl_info = {
    "name": "Mothion Link",
    "author": "Mothion",
    "version": (0, 1, 0),
    "blender": (5, 1, 0),
    "description": "Shows the scene in Mothion, live",
    "category": "Import-Export",
}

import json
import os
import socket
import struct
import time
import traceback

import bpy

# Mothion listens on the first of these that is free (see MOTHION_LINK_PORT to say another).
DEFAULT_PORTS = (47653, 47654, 47655, 47656)
PROTOCOL = 1
QUIET_SECONDS = 0.5      # a change is told once nothing has changed for this long
RECONNECT_SECONDS = 2.0
TICK_BUSY = 0.0          # between two frames of a request: as soon as the window has had its turn
TICK_IDLE = 0.1
HIDDEN_NAME = "Mothion (hidden)"
WORKSPACE_NAME = "Mothion"

_link = None


def _ports():
    forced = os.environ.get("MOTHION_LINK_PORT")
    return (int(forced),) if forced else DEFAULT_PORTS


def _pack(header, payload=b""):
    head = json.dumps(header, separators=(",", ":")).encode("utf-8")
    return struct.pack("<II", len(head), len(payload)) + head + payload


def _view3d_of(area):
    region = next((r for r in area.regions if r.type == 'WINDOW'), None)
    return (area.spaces.active, region) if region and region.width > 1 and region.height > 1 else None


def _find_view3d():
    """A 3D viewport to draw with. The one of the workspace made for this (see _stage_view3d) when there is one: its
    shading is the add-on's to set, and the person's own viewports never change. Failing that, one that is not on
    screen, and failing that one that is shown (its shading is then put back after every request)."""
    manager = bpy.context.window_manager
    workspace = bpy.data.workspaces.get(WORKSPACE_NAME)
    if workspace is not None:
        for screen in workspace.screens:
            for area in screen.areas:
                if area.type == 'VIEW_3D' and _view3d_of(area):
                    return _view3d_of(area)
    shown = {window.screen.as_pointer() for window in manager.windows}
    for other in bpy.data.workspaces:
        for screen in other.screens:
            if screen.as_pointer() not in shown:
                for area in screen.areas:
                    if area.type == 'VIEW_3D' and _view3d_of(area):
                        return _view3d_of(area)
    windows = list(manager.windows)
    active = bpy.context.window
    windows.sort(key=lambda w: w != active)
    for window in windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D' and _view3d_of(area):
                return _view3d_of(area)
    return None, None


def _stage_view3d():
    """The workspace "Mothion", made once (a copy of one that has a 3D viewport), whose viewport the frames are drawn
    with. It is never shown, and stays in the file like any workspace."""
    if bpy.data.workspaces.get(WORKSPACE_NAME) is not None:
        return None
    try:
        window = bpy.context.window or bpy.context.window_manager.windows[0]
        current = window.workspace
        source = current if any(a.type == 'VIEW_3D' for s in current.screens for a in s.areas) else next(
            (w for w in bpy.data.workspaces if any(a.type == 'VIEW_3D' for s in w.screens for a in s.areas)), None)
        if source is None:
            return None
        before = {w.name for w in bpy.data.workspaces}
        with bpy.context.temp_override(window=window, workspace=source):
            bpy.ops.workspace.duplicate()
        made = next((w for w in bpy.data.workspaces if w.name not in before), None)
        if made is not None:
            made.name = WORKSPACE_NAME
        # (Blender moves the window to the new workspace a moment later, not now: see Link.stage_ready)
        return window, current
    except Exception:
        traceback.print_exc()
    return None


_COPIED_STRUCTS = ("render", "eevee", "display", "display_settings", "view_settings", "unit_settings")
_NOT_COPIED = {"name", "rna_type", "frame_current", "frame_float", "use_fake_user", "use_extra_user", "compositing_node_group"}


def _copy_properties(source, target):
    for prop in source.bl_rna.properties:
        name = prop.identifier
        if prop.is_readonly or name in _NOT_COPIED or prop.type == 'COLLECTION':
            continue
        try:
            if prop.type == 'POINTER':
                if name in ("camera", "world") and getattr(target, name) != getattr(source, name):
                    setattr(target, name, getattr(source, name))
                continue
            value = getattr(source, name)
            if getattr(target, name) != value:  # (writing what is the same already would still tag an update)
                setattr(target, name, value)
        except Exception:
            pass


def _sync_collection(source, target):
    for child in [c for c in target.children if c.name not in source.children]:
        target.children.unlink(child)
    for child in [c for c in source.children if c.name not in target.children]:
        target.children.link(child)
    for ob in [o for o in target.objects if o.name not in source.objects]:
        target.objects.unlink(ob)
    for ob in [o for o in source.objects if o.name not in target.objects]:
        target.objects.link(ob)


def _sync_layer_collection(source, target):
    for name in ("exclude", "hide_viewport", "holdout", "indirect_only"):
        try:
            if getattr(target, name) != getattr(source, name):
                setattr(target, name, getattr(source, name))
        except Exception:
            pass
    for child in source.children:
        mine = target.children.get(child.name)
        if mine is not None:
            _sync_layer_collection(child, mine)


def _sync_scene(scene, hidden):
    """Makes the hidden scene what the real one is: its settings, what it contains, what is shown, and the compositor."""
    try:
        _copy_properties(scene, hidden)
        for name in _COPIED_STRUCTS:
            if hasattr(scene, name) and hasattr(hidden, name):
                _copy_properties(getattr(scene, name), getattr(hidden, name))
        _sync_collection(scene.collection, hidden.collection)
        source, target = bpy.context.view_layer, hidden.view_layers[0]
        target.update()
        _sync_layer_collection(source.layer_collection, target.layer_collection)
        for ob in source.objects:
            try:
                hide = ob.hide_get(view_layer=source)
                if ob.hide_get(view_layer=target) != hide:
                    ob.hide_set(hide, view_layer=target)
            except Exception:
                pass
        group = getattr(scene, "compositing_node_group", None)
        old = getattr(hidden, "compositing_node_group", None)
        if group is None:
            hidden.compositing_node_group = None
        else:
            # The compositor of the viewport reads the render layers of the scene a node names: the copy needs its own.
            mine = group.copy()
            for node in mine.nodes:
                if node.bl_idname == "CompositorNodeRLayers" and node.scene == scene:
                    node.scene = hidden
            hidden.compositing_node_group = mine
        if old is not None and old != getattr(hidden, "compositing_node_group", None) and old.users == 0:
            bpy.data.node_groups.remove(old)
    except Exception:
        traceback.print_exc()


def _pixels(buffer, width, height):
    """The pixels of a read-back texture as height x width x RGBA bytes, top row first, with the colors premultiplied.

    The buffer of the gpu module reports a shape and strides that are not those of its memory (read as it says, an
    image comes out in stripes): the memory is the rows of the picture, bottom row first, one RGBA pixel after the other.
    """
    import numpy as np
    view = np.asarray(buffer)
    flat = np.lib.stride_tricks.as_strided(view, shape=(view.nbytes,), strides=(1,))
    pixels = flat.reshape(height, width, 4)[::-1]
    alpha = pixels[..., 3]
    edge = (alpha > 0) & (alpha < 255)
    if edge.any():
        # The viewport draws over nothing in linear color, premultiplied, and then encodes the colors for the screen: at
        # the soft edges the colors came out brighter than their alpha lets them be. They are put right: back to linear,
        # divided by the alpha, encoded again and multiplied by the alpha (what a program that composites expects).
        pixels = np.array(pixels)
        a = pixels[..., 3][edge].astype(np.float32) / 255.0
        v = pixels[..., :3][edge].astype(np.float32) / 255.0
        linear = np.where(v <= 0.04045, v / 12.92, ((v + 0.055) / 1.055) ** 2.4)
        straight = np.clip(linear / a[:, None], 0.0, 1.0)
        shown = np.where(straight <= 0.0031308, straight * 12.92, 1.055 * straight ** (1 / 2.4) - 0.055)
        pixels[..., :3][edge] = np.clip(shown * a[:, None] * 255.0 + 0.5, 0, 255).astype(np.uint8)
    return np.ascontiguousarray(pixels)


class Link:
    def __init__(self):
        self.sock = None
        self.buffer = bytearray()
        self.next_try = 0.0
        self.job = None            # the request being served
        self.pending = []          # requests waiting
        self.dirty_since = None    # a change that is not told yet
        self.last_frame = None
        self.offscreen = None
        self.offscreen_size = None
        self.hello_sent_for = None
        self.signature = None      # what Mothion was last told of the scene (its range, camera, speed): not a depsgraph update
        self.stage_restore = None  # (window, workspace, since) while the person is being brought back from the new workspace

    # -- the connection ------------------------------------------------------------------------------------------

    def connect(self):
        now = time.time()
        if now < self.next_try:
            return
        self.next_try = now + RECONNECT_SECONDS
        for port in _ports():
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=0.3)
            except OSError:
                continue
            sock.setblocking(False)
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            self.sock = sock
            self.buffer.clear()
            self.send(self.hello())
            return

    def close(self):
        if self.sock:
            try:
                self.sock.close()
            except OSError:
                pass
        self.sock = None
        self.job = None
        self.pending.clear()

    def send(self, header, payload=b""):
        if not self.sock:
            return False
        data = _pack(header, payload)
        view = memoryview(data)
        try:
            while view:
                try:
                    sent = self.sock.send(view)
                    view = view[sent:]
                except BlockingIOError:
                    time.sleep(0.001)  # the other side is slow: a frame is a few megabytes
        except OSError:
            self.close()
            return False
        return True

    def receive(self):
        if not self.sock:
            return
        try:
            while True:
                chunk = self.sock.recv(1 << 20)
                if not chunk:
                    self.close()
                    return
                self.buffer += chunk
        except BlockingIOError:
            pass
        except OSError:
            self.close()
            return
        while len(self.buffer) >= 8:
            head_len, payload_len = struct.unpack_from("<II", self.buffer)
            if len(self.buffer) < 8 + head_len + payload_len:
                break
            header = json.loads(bytes(self.buffer[8:8 + head_len]).decode("utf-8"))
            del self.buffer[:8 + head_len + payload_len]
            self.handle(header)

    # -- what is told about the scene ------------------------------------------------------------------------------

    def hello(self):
        scene = bpy.context.scene
        space, _region = _find_view3d()
        return {
            "type": "hello", "protocol": PROTOCOL, "blender": bpy.app.version_string, "pid": os.getpid(),
            "file": bpy.data.filepath, "scene": scene.name,
            "camera": scene.camera.name if scene.camera else "",
            "frameStart": scene.frame_start, "frameEnd": scene.frame_end, "frame": scene.frame_current,
            "fps": scene.render.fps / scene.render.fps_base, "viewport": space is not None,
        }

    def mark_changed(self):
        self.dirty_since = time.time()

    # -- the requests ------------------------------------------------------------------------------------------------

    def handle(self, message):
        kind = message.get("type")
        if kind == "render":
            self.pending.append(message)
        elif kind == "cancel":
            self.pending = [m for m in self.pending if m.get("id") != message.get("id")]
            if self.job and self.job["id"] == message.get("id"):
                self.finish_job(cancelled=True)
        elif kind == "info":
            self.send(self.hello())

    def start_job(self, message):
        scene = bpy.context.scene
        space, region = _find_view3d()
        job = {"id": message.get("id"), "frames": list(message.get("frames", [])), "width": int(message["width"]),
               "height": int(message["height"]), "options": message, "scene": scene, "space": space, "region": region,
               "saved": None}
        if scene.camera is None:
            self.send({"type": "error", "id": job["id"], "message": "The scene has no active camera."})
            return
        if space is None:
            self.send({"type": "error", "id": job["id"], "message": "There is no 3D viewport in this Blender window."})
            return
        shading = space.shading
        job["saved"] = {
            "frame": scene.frame_current, "type": shading.type, "compositor": shading.use_compositor,
            "overlays": space.overlay.show_overlays,
        }
        # How the viewport draws for the capture: what was asked, overlays (grid, outlines) never.
        wanted = message.get("shading", "KEEP")
        if wanted != "KEEP":
            shading.type = wanted
        wanted = message.get("compositor", "KEEP")
        if wanted != "KEEP":
            shading.use_compositor = wanted
        space.overlay.show_overlays = False
        self._hide(job)
        self.job = job

    def _hide(self, job):
        """The scene the frames are drawn on: a copy of the person's own, made once and never removed (removing it was
        what crashed Blender). It has its own timeline and its own evaluation, so the real scene (its current frame,
        what is being dragged) is never touched. The objects are shared, not duplicated, so every edit is seen; the rest
        is made the same as the real scene's at the start of every request."""
        scene = job["scene"]
        hidden = bpy.data.scenes.get(HIDDEN_NAME)
        if hidden is None:
            hidden = scene.copy()
            hidden.name = HIDDEN_NAME
        _sync_scene(scene, hidden)
        job["hidden"] = hidden

    def finish_job(self, cancelled=False, error=None):
        job = self.job
        self.job = None
        if not job:
            return
        try:
            saved = job["saved"]
            space = job["space"]
            space.shading.type = saved["type"]
            space.shading.use_compositor = saved["compositor"]
            space.overlay.show_overlays = saved["overlays"]
        except Exception:
            traceback.print_exc()
        if error:
            self.send({"type": "error", "id": job["id"], "message": error})
        self.send({"type": "done", "id": job["id"], "cancelled": cancelled, "ok": error is None and not cancelled})

    def _watch_scene_settings(self):
        """The range of frames, the camera and the speed are not geometry: Blender does not say they changed, so they
        are looked at here, and a change of them is told like any other."""
        scene = bpy.context.scene
        signature = (bpy.data.filepath, scene.name, scene.frame_start, scene.frame_end,
                     scene.camera.name if scene.camera else "", scene.render.fps, scene.render.fps_base)
        if self.signature is not None and signature != self.signature:
            self.mark_changed()
        self.signature = signature

    def stage_ready(self):
        """Makes the workspace the frames are drawn in, once, and gives the person back their own: Blender switches the
        window to a new workspace a moment after it is made, so that is waited for and undone. False until all is done."""
        try:
            if self.stage_restore is None:
                made = _stage_view3d()
                if made is None:
                    return True
                self.stage_restore = (made[0], made[1], time.time())
                return False
            window, workspace, since = self.stage_restore
            if window.workspace.name == WORKSPACE_NAME:
                window.workspace = workspace
                return False
            if time.time() - since < 0.6 and window.workspace == workspace:
                return False  # (the switch may not have come yet)
            self.stage_restore = None
            return True
        except Exception:
            traceback.print_exc()
            self.stage_restore = None
            return True

    def serve(self):
        """One frame of the request being served (or the start of the next one)."""
        import gpu
        import numpy as np
        if self.job is None:
            if not self.pending:
                return False
            if not self.stage_ready():
                return False
            self.start_job(self.pending.pop(0))
            return True
        job = self.job
        if not job["frames"]:
            self.finish_job()
            return True
        frame = job["frames"].pop(0)
        scene, space, region = job["scene"], job["space"], job["region"]
        width, height = job["width"], job["height"]
        try:
            if self.offscreen is None or self.offscreen_size != (width, height):
                if self.offscreen:
                    self.offscreen.free()
                self.offscreen = gpu.types.GPUOffScreen(width, height)
                self.offscreen_size = (width, height)
            hidden = job["hidden"]
            view_layer = hidden.view_layers[0]
            hidden.frame_set(frame)
            depsgraph = view_layer.depsgraph
            camera = depsgraph.id_eval_get(hidden.camera)
            view_matrix = camera.matrix_world.inverted()
            projection = camera.calc_matrix_camera(depsgraph, x=width, y=height)
            self.offscreen.draw_view3d(hidden, view_layer, space, region, view_matrix, projection,
                                       do_color_management=True, draw_background=bool(job["options"].get("background", False)))
            pixels = _pixels(self.offscreen.texture_color.read(), width, height)
            self.send({"type": "frame", "id": job["id"], "frame": frame, "width": width, "height": height,
                       "format": "rgba8", "premultiplied": True}, pixels.tobytes())
        except Exception as error:
            traceback.print_exc()
            self.finish_job(error="A frame could not be drawn: %s" % error)
        return True

    # -- the timer ---------------------------------------------------------------------------------------------------

    def tick(self):
        if self.sock is None:
            self.connect()
            return RECONNECT_SECONDS / 4
        self.receive()
        if self.sock is None:
            return TICK_IDLE
        self._watch_scene_settings()
        quiet = self.dirty_since is None or time.time() - self.dirty_since > QUIET_SECONDS
        if self.dirty_since is not None and quiet:
            self.dirty_since = None
            self.send(dict(self.hello(), type="changed"))
        # While something is being changed (a drag, a slider) nothing is drawn: it would only slow the work down, and
        # the frames would be out of date at once.
        busy = self.serve() if quiet else False
        return TICK_BUSY if busy else TICK_IDLE


def _depsgraph_update(scene, depsgraph):
    link = _link
    if link is None or link.sock is None:
        return
    # (the hidden copy that the frames are drawn on is evaluated too: that is not a change)
    if scene.name == HIDDEN_NAME:
        return
    # Moving along the timeline is not a change of the scene; anything else that is updated is.
    frame = bpy.context.scene.frame_current
    only_frame = frame != link.last_frame
    link.last_frame = frame
    real = False
    for update in depsgraph.updates:
        if isinstance(update.id, bpy.types.Scene) and only_frame:
            continue
        if isinstance(update.id, bpy.types.Collection):
            continue  # (making the hidden copy links the objects into a collection of its own; the objects say the rest)
        # (the shading of an object alone is also what making the hidden copy does to the shared objects)
        shading = update.is_updated_shading and not isinstance(update.id, bpy.types.Object)
        if update.is_updated_geometry or update.is_updated_transform or shading:
            real = True
            break
    if real:
        link.mark_changed()


def _tick():
    try:
        return _link.tick() if _link else None
    except Exception:
        traceback.print_exc()
        return 1.0  # whatever it was, it must not stop the timer: it tries again


def register():
    global _link
    if _link is not None:
        return
    _link = Link()
    bpy.app.handlers.depsgraph_update_post.append(_depsgraph_update)
    bpy.app.timers.register(_tick, first_interval=0.5, persistent=True)


def unregister():
    global _link
    if bpy.app.timers.is_registered(_tick):
        bpy.app.timers.unregister(_tick)
    if _depsgraph_update in bpy.app.handlers.depsgraph_update_post:
        bpy.app.handlers.depsgraph_update_post.remove(_depsgraph_update)
    if _link:
        if _link.job is not None:
            _link.finish_job(cancelled=True)
        _link.close()
    _link = None


if __name__ == "__main__":
    register()
