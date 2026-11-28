#include "canvas/ModelRenderer.h"

#include "core/rig3d/Pose.h"
#include "core/rig3d/Skinning.h"
#include "core/rig3d/SoftwareRaster.h"

#include <QCoreApplication>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QThread>

#include <map>
#include <memory>
#include <mutex>

namespace mothion::canvas {

namespace {

bool g_usedOpenGL = false;

// The textures of a model decoded, for the CPU: kept while the model is (by the source's address and a weak pointer to
// know it is still the same one).
struct DecodedTextures
{
    std::weak_ptr<const doc::ModelSource> source;
    std::vector<rig3d::TexturePixels> textures;
    std::vector<QImage> images; // the same, for OpenGL
};

std::mutex g_texturesLock;
std::map<const doc::ModelSource *, DecodedTextures> g_textures;

const DecodedTextures &texturesOf(const std::shared_ptr<const doc::ModelSource> &source)
{
    std::lock_guard lock(g_texturesLock);
    // Those of models that are gone are forgotten.
    for (auto it = g_textures.begin(); it != g_textures.end();)
        it = it->second.source.expired() ? g_textures.erase(it) : std::next(it);
    DecodedTextures &d = g_textures[source.get()];
    if (d.source.lock() == source)
        return d;
    d = {};
    d.source = source;
    for (const rig3d::Image &image : source->model->images) {
        QImage decoded = QImage::fromData(image.bytes.data(), int(image.bytes.size()));
        rig3d::TexturePixels t;
        if (!decoded.isNull()) {
            decoded = decoded.convertToFormat(QImage::Format_ARGB32);
            t.width = decoded.width();
            t.height = decoded.height();
            t.pixels.resize(std::size_t(t.width) * std::size_t(t.height));
            for (int y = 0; y < t.height; ++y) {
                const auto *row = reinterpret_cast<const std::uint32_t *>(decoded.constScanLine(y));
                std::copy(row, row + t.width, t.pixels.begin() + std::ptrdiff_t(y) * t.width);
            }
        }
        d.textures.push_back(std::move(t));
        d.images.push_back(std::move(decoded));
    }
    return d;
}

QImage renderSoftware(const doc::ModelRig &rig, const rig3d::Pose &pose, const std::vector<rig3d::PosedPrimitive> &meshes,
                      const rig3d::Projection &projection, QSize size)
{
    std::vector<std::uint32_t> pixels;
    rig3d::rasterize(*rig.model(), meshes, projection, pose.camera.eye(), texturesOf(rig.source).textures, size.width(),
                     size.height(), pixels);
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < size.height(); ++y)
        std::copy(pixels.begin() + std::ptrdiff_t(y) * size.width(), pixels.begin() + std::ptrdiff_t(y + 1) * size.width(),
                  reinterpret_cast<std::uint32_t *>(out.scanLine(y)));
    return out;
}

const char *kVertexShader = R"(#version 120
attribute vec3 position;
attribute vec3 normal;
attribute vec2 uv;
uniform mat4 viewProjection;
varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vUv;
void main()
{
    vPosition = position;
    vNormal = normal;
    vUv = uv;
    gl_Position = viewProjection * vec4(position, 1.0);
}
)";

// The same light as the CPU's: ambient 0.35 plus 0.65 of Lambert from the camera, in linear color, out in sRGB and
// premultiplied.
const char *kFragmentShader = R"(#version 120
varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vUv;
uniform vec3 eye;
uniform vec4 baseColor;
uniform bool textured;
uniform bool doubleSided;
uniform bool blended;
uniform sampler2D texture0;
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(0.04045, c)); }
vec3 toSrgb(vec3 c) { c = clamp(c, 0.0, 1.0); return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(0.0031308, c)); }
void main()
{
    vec4 color = baseColor;
    if (textured) {
        vec4 t = texture2D(texture0, vUv);
        color *= vec4(toLinear(t.rgb), t.a);
    }
    if (!blended) {
        if (color.a < 0.5)
            discard;
        color.a = 1.0;
    }
    float facing = dot(normalize(vNormal), normalize(eye - vPosition));
    float lambert = doubleSided ? abs(facing) : max(0.0, facing);
    vec3 lit = color.rgb * (0.35 + 0.65 * lambert);
    gl_FragColor = vec4(toSrgb(lit) * color.a, color.a);
}
)";

// OpenGL, made once on the application's thread: a context on an offscreen surface, the shaders, the framebuffer (made
// again for another size) and the textures of the models drawn.
class GlRenderer : protected QOpenGLFunctions
{
public:
    // Made at the first call; let go as the application ends (while its OpenGL still is there to free what it holds).
    static GlRenderer *instance()
    {
        static GlRenderer *renderer = nullptr;
        static bool tried = false;
        if (!tried) {
            tried = true;
            auto *r = new GlRenderer;
            if (r->init()) {
                renderer = r;
                qAddPostRoutine([] {
                    delete renderer;
                    renderer = nullptr;
                });
            } else {
                delete r;
            }
        }
        return renderer;
    }

    bool init()
    {
        m_surface.setFormat(QSurfaceFormat::defaultFormat());
        m_surface.create();
        if (!m_surface.isValid())
            return false;
        if (!m_context.create() || !m_context.makeCurrent(&m_surface))
            return false;
        initializeOpenGLFunctions();
        const bool ok = m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader)
                        && m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) && m_program.link();
        m_context.doneCurrent();
        return ok;
    }

    QImage render(const doc::ModelRig &rig, const rig3d::Pose &pose, const std::vector<rig3d::PosedPrimitive> &meshes,
                  const rig3d::Projection &projection, QSize size)
    {
        if (!m_context.makeCurrent(&m_surface))
            return {};
        if (!m_fbo || m_fbo->size() != size) {
            QOpenGLFramebufferObjectFormat format;
            format.setAttachment(QOpenGLFramebufferObject::Depth);
            format.setSamples(4);
            m_fbo = std::make_unique<QOpenGLFramebufferObject>(size, format);
        }
        if (!m_fbo->isValid()) {
            m_context.doneCurrent();
            return {};
        }
        m_fbo->bind();
        glViewport(0, 0, size.width(), size.height());
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glFrontFace(GL_CCW);
        m_program.bind();
        QMatrix4x4 vp;
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c)
                vp(r, c) = float(projection.viewProjection(r, c));
        m_program.setUniformValue("viewProjection", vp);
        const rig3d::Vec3 eye = pose.camera.eye();
        m_program.setUniformValue("eye", QVector3D(float(eye.x), float(eye.y), float(eye.z)));
        m_program.setUniformValue("texture0", 0);
        const rig3d::Model &model = *rig.model();
        const DecodedTextures &decoded = texturesOf(rig.source);
        // The opaque first, then those that are see-through over them (without hiding what is behind).
        for (bool blendPass : {false, true}) {
            if (blendPass) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
                glDepthMask(GL_FALSE);
            }
            for (const rig3d::PosedPrimitive &posed : meshes) {
                const rig3d::Primitive &prim = model.meshes[std::size_t(posed.mesh)].primitives[std::size_t(posed.primitive)];
                const rig3d::Material material = prim.material >= 0 ? model.materials[std::size_t(prim.material)] : rig3d::Material{};
                if (material.blend != blendPass || prim.indices.empty())
                    continue;
                if (material.doubleSided)
                    glDisable(GL_CULL_FACE);
                else
                    glEnable(GL_CULL_FACE);
                QOpenGLTexture *texture = material.texture >= 0 ? textureFor(rig.source, decoded, material.texture) : nullptr;
                m_program.setUniformValue("baseColor", QVector4D(material.baseColor[0], material.baseColor[1],
                                                                 material.baseColor[2], material.baseColor[3]));
                m_program.setUniformValue("textured", texture != nullptr && !prim.uvs.empty());
                m_program.setUniformValue("doubleSided", material.doubleSided);
                m_program.setUniformValue("blended", material.blend);
                if (texture)
                    texture->bind(0);
                draw(posed, prim);
            }
        }
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        m_program.release();
        m_fbo->release();
        QImage image = m_fbo->toImage(true);
        m_context.doneCurrent();
        return image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

private:
    void draw(const rig3d::PosedPrimitive &posed, const rig3d::Primitive &prim)
    {
        auto attribute = [this](const char *name, QOpenGLBuffer &buffer, const std::vector<float> &data, int size) {
            const int location = m_program.attributeLocation(name);
            if (location < 0)
                return;
            if (data.empty()) {
                m_program.disableAttributeArray(location);
                m_program.setAttributeValue(location, 0.0f, 0.0f);
                return;
            }
            buffer.bind();
            buffer.allocate(data.data(), int(data.size() * sizeof(float)));
            m_program.enableAttributeArray(location);
            m_program.setAttributeBuffer(location, GL_FLOAT, 0, size);
        };
        for (QOpenGLBuffer *b : {&m_positions, &m_normals, &m_uvs})
            if (!b->isCreated())
                b->create();
        if (!m_indices.isCreated())
            m_indices.create();
        attribute("position", m_positions, posed.positions, 3);
        attribute("normal", m_normals, posed.normals, 3);
        attribute("uv", m_uvs, prim.uvs, 2);
        m_indices.bind();
        m_indices.allocate(prim.indices.data(), int(prim.indices.size() * sizeof(std::uint32_t)));
        glDrawElements(GL_TRIANGLES, GLsizei(prim.indices.size()), GL_UNSIGNED_INT, nullptr);
        m_indices.release();
        QOpenGLBuffer::release(QOpenGLBuffer::VertexBuffer);
    }

    QOpenGLTexture *textureFor(const std::shared_ptr<const doc::ModelSource> &source, const DecodedTextures &decoded, int index)
    {
        if (index < 0 || std::size_t(index) >= decoded.images.size() || decoded.images[std::size_t(index)].isNull())
            return nullptr;
        for (auto it = m_textures.begin(); it != m_textures.end();) // those of models that are gone
            it = it->first.second.expired() ? m_textures.erase(it) : std::next(it);
        auto &slot = m_textures[{{source.get(), index}, source}];
        if (!slot) {
            slot = std::make_unique<QOpenGLTexture>(decoded.images[std::size_t(index)]);
            slot->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
            slot->setMagnificationFilter(QOpenGLTexture::Linear);
            slot->setWrapMode(QOpenGLTexture::Repeat);
        }
        return slot.get();
    }

    struct TextureKey
    {
        std::pair<const doc::ModelSource *, int> first;
        std::weak_ptr<const doc::ModelSource> second;
        bool operator<(const TextureKey &o) const { return first < o.first; }
    };

    QOffscreenSurface m_surface;
    QOpenGLContext m_context;
    QOpenGLShaderProgram m_program;
    std::unique_ptr<QOpenGLFramebufferObject> m_fbo;
    QOpenGLBuffer m_positions{QOpenGLBuffer::VertexBuffer}, m_normals{QOpenGLBuffer::VertexBuffer},
        m_uvs{QOpenGLBuffer::VertexBuffer}, m_indices{QOpenGLBuffer::IndexBuffer};
    std::map<TextureKey, std::unique_ptr<QOpenGLTexture>> m_textures;
};

} // namespace

QImage renderModel(const doc::ModelRig &rig, const std::vector<double> &numbers, QSize size)
{
    if (rig.isEmpty() || size.isEmpty())
        return {};
    const rig3d::Model &model = *rig.model();
    const rig3d::Pose pose = rig3d::Pose::fromNumbers(model, numbers);
    const std::vector<rig3d::PosedPrimitive> meshes = rig3d::posedMeshes(model, pose);
    const rig3d::Projection projection = rig3d::projectionOf(model, pose.camera, size.width(), size.height());
    const bool gui = QCoreApplication::instance() && QThread::currentThread() == QCoreApplication::instance()->thread();
    if (gui && qEnvironmentVariableIntValue("MOTHION_SOFTWARE_3D") == 0)
        if (GlRenderer *gl = GlRenderer::instance()) {
            QImage image = gl->render(rig, pose, meshes, projection, size);
            if (!image.isNull()) {
                g_usedOpenGL = true;
                return image;
            }
        }
    g_usedOpenGL = false;
    return renderSoftware(rig, pose, meshes, projection, size);
}

bool modelRendererUsedOpenGL()
{
    return g_usedOpenGL;
}

} // namespace mothion::canvas
