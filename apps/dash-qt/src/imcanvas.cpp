#include "imcanvas.hpp"

#include "engine.hpp"
#include "log.hpp"
#include "ui.hpp"

#include <imgui.h>

#include <QCursor>
#include <QImage>
#include <QQuickWindow>
#include <QSGClipNode>
#include <QSGGeometryNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <map>
#include <set>

namespace {

engine::Engine *g_eng = nullptr;
ImFontAtlas *g_atlas = nullptr;
ui::Fonts g_fonts;
ui::VrEnv g_env;
QImage g_atlasImage;
QImage g_panelImage;
int g_panelVersion = 0;
std::set<ImCanvas *> g_all;
int g_tick = 0;

constexpr int TEX_ATLAS = 1, TEX_PANEL = 2; // ImTextureID values

// ---- material: ImGui vertices with a texture ----------------------------------------------------------------------
class ImShader : public QSGMaterialShader {
public:
    ImShader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/imcanvas.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/imcanvas.frag.qsb"));
    }
    bool updateUniformData(RenderState &s, QSGMaterial *, QSGMaterial *) override
    {
        QByteArray *b = s.uniformData();
        if (s.isMatrixDirty()) std::memcpy(b->data(), s.combinedMatrix().constData(), 64);
        if (s.isOpacityDirty()) {
            const float o = s.opacity();
            std::memcpy(b->data() + 64, &o, 4);
        }
        return true;
    }
    void updateSampledImage(RenderState &s, int binding, QSGTexture **t, QSGMaterial *n, QSGMaterial *) override;
};

class ImMaterial : public QSGMaterial {
public:
    QSGTexture *tex = nullptr;
    ImMaterial() { setFlag(Blending); }
    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override { return new ImShader; }
    int compare(const QSGMaterial *o) const override
    {
        const auto *m = static_cast<const ImMaterial *>(o);
        return tex == m->tex ? 0 : tex < m->tex ? -1 : 1;
    }
};

void ImShader::updateSampledImage(RenderState &s, int binding, QSGTexture **t, QSGMaterial *n, QSGMaterial *)
{
    if (binding != 1) return;
    auto *m = static_cast<ImMaterial *>(n);
    if (!m->tex) return;
    m->tex->commitTextureOperations(s.rhi(), s.resourceUpdateBatch());
    *t = m->tex;
}

const QSGGeometry::AttributeSet &imAttributes()
{
    static const QSGGeometry::Attribute a[] = {
        QSGGeometry::Attribute::createWithAttributeType(0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(1, 2, QSGGeometry::FloatType, QSGGeometry::TexCoordAttribute),
        QSGGeometry::Attribute::createWithAttributeType(2, 4, QSGGeometry::UnsignedByteType, QSGGeometry::ColorAttribute),
    };
    static const QSGGeometry::AttributeSet set = {3, int(sizeof(ImDrawVert)), a};
    return set;
}

// textures per window (the scene graph's textures belong to a window's renderer)
struct WindowTextures {
    QSGTexture *atlas = nullptr;
    QSGTexture *panel = nullptr;
    int panelVersion = -1;
    std::map<intptr_t, std::pair<QSGTexture *, int>> dyn; // texture, version
};
struct Dynamic {
    QImage img;
    int version = 0;
};
std::map<intptr_t, Dynamic> g_dyn;
std::map<std::string, intptr_t> g_dynSlots;
std::map<QQuickWindow *, WindowTextures> g_textures;

ImFont *addScaled(ImGuiIO &io, std::initializer_list<const char *> files, float size, float bake, const ImWchar *ranges = nullptr)
{
    ImFont *f = engine::addFont(io, files, size * bake, ranges);
    f->Scale = 1.f / bake;
    return f;
}

} // namespace

// ---- setup ---------------------------------------------------------------------------------------------------------
void ImCanvas::setup(engine::Engine *e, float bake)
{
    g_eng = e;
    bake = std::clamp(bake, 0.5f, 3.f);
    g_atlas = new ImFontAtlas;
    // the fonts of the ImGui desktop window (see the old main.cpp)
    ImGuiContext *tmp = ImGui::CreateContext(g_atlas);
    ImGui::SetCurrentContext(tmp);
    ImGuiIO &io = ImGui::GetIO();
    const auto cjk = {"C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"};
    const auto cjkBold = {"C:/Windows/Fonts/msyhbd.ttc", "C:/Windows/Fonts/msyh.ttc", "C:/Windows/Fonts/simhei.ttf"};
    const auto nums = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeui.ttf", "C:/Windows/Fonts/arial.ttf"};
    static ImVector<ImWchar> uiRanges;
    {
        ImFontGlyphRangesBuilder b;
        b.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        b.AddText(
#include "ui_chars.inc"
        );
        b.BuildRanges(&uiRanges);
    }
    // the common Chinese set, not all 20 000+ characters: the UI is English and the atlas at a 4K screen's scale
    // would take hundreds of MB
    g_fonts.text = addScaled(io, cjk, 18.f, bake, uiRanges.Data);
    g_fonts.title = addScaled(io, cjkBold, 24.f, bake, uiRanges.Data);
    g_fonts.num = addScaled(io, nums, 19.f, bake);
    g_fonts.numMid = addScaled(io, nums, 32.f, bake);
    g_fonts.numBig = addScaled(io, nums, 64.f, bake);
    g_fonts.small = addScaled(io, cjk, 15.f, bake, uiRanges.Data);
    g_fonts.numSmall = addScaled(io, nums, 16.f, bake);
    unsigned char *px = nullptr;
    int w = 0, h = 0;
    g_atlas->GetTexDataAsRGBA32(&px, &w, &h);
    LOG("desktop font atlas %d x %d (scale %.2f)", w, h, bake);
    g_atlasImage = QImage(px, w, h, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    g_atlas->SetTexID(ImTextureID(intptr_t(TEX_ATLAS)));
    ImGui::DestroyContext(tmp);

    // what the VR page needs
    g_env.vr = &e->vr;
    g_env.hmd = &e->hmd;
    g_env.layoutHmd = &e->layoutHmd;
    g_env.fits = e->fits;
    g_env.texSize = e->texSize;
    g_env.texScale = e->texScale;
    g_env.panelTexture = reinterpret_cast<void *>(intptr_t(TEX_PANEL));
    g_env.status = &e->vrStatus;
    g_env.reconnect = [e] { e->startVr(false); };
    g_env.launch = [e] { e->startVr(true); };
}

void *ImCanvas::dynamicTexture(const std::string &slot, const QImage &img)
{
    auto it = g_dynSlots.find(slot);
    const intptr_t id = it != g_dynSlots.end() ? it->second : (g_dynSlots[slot] = 100 + intptr_t(g_dynSlots.size()));
    Dynamic &d = g_dyn[id];
    d.img = img;
    d.version++;
    return reinterpret_cast<void *>(id);
}

void ImCanvas::tickAll()
{
    g_tick++;
    bool vrPage = false;
    for (ImCanvas *c : g_all) {
        if (!c->isVisible()) continue;
        if (c->piece_ == "page" && c->page_ == 6) vrPage = true;
        c->polish();
        c->update();
    }
    // the HMD texture for the VR page's preview
    if (vrPage && g_eng && g_tick % 2 == 0) { // 30 a second: smooth enough to judge the panels by
        static std::vector<uint8_t> buf;
        unsigned n = 0;
        if (g_eng->readPanel(buf, n) && n) {
            g_panelImage = QImage(buf.data(), int(n), int(n), QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGBA8888_Premultiplied);
            g_panelVersion++;
        }
    }
}

// ---- item ----------------------------------------------------------------------------------------------------------
ImCanvas::ImCanvas(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::LeftButton | Qt::RightButton | Qt::MiddleButton);
    setAcceptHoverEvents(true);
    g_all.insert(this);
    last_ = std::chrono::steady_clock::now();
}

ImCanvas::~ImCanvas()
{
    g_all.erase(this);
    if (ctx_) ImGui::DestroyContext(ctx_);
}

void ImCanvas::setPiece(const QString &p)
{
    if (p == piece_) return;
    piece_ = p;
    emit changed();
    kick();
}

void ImCanvas::setPageIndex(int p)
{
    if (p == page_) return;
    page_ = p;
    emit changed();
    kick();
}

void ImCanvas::setPadding(QSizeF p)
{
    if (p == pad_) return;
    pad_ = p;
    emit changed();
    kick();
}

void ImCanvas::kick()
{
    polish();
    update();
}

void ImCanvas::updatePolish()
{
    if (g_eng && g_atlas && width() >= 1 && height() >= 1) frame();
}

void ImCanvas::frame()
{
    if (!ctx_) {
        ctx_ = ImGui::CreateContext(g_atlas);
        ImGui::SetCurrentContext(ctx_);
        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        ui::setupStyle();
    }
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - last_).count(), 1e-4f, 0.5f);
    last_ = now;
    const float W = float(width()), H = float(height());
    io.DisplaySize = ImVec2(W, H);

    ui::bind(g_fonts);
    app::App &a = g_eng->a;
    const timing::Snapshot &s = g_eng->snap;
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(float(pad_.width()), float(pad_.height())));
    ImGui::Begin("##canvas", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    const ImVec2 av = ImGui::GetContentRegionAvail();
    if (piece_ == "sidebar") {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ui::drawSidebar(a);
        ImGui::PopStyleVar();
    } else if (piece_ == "topbar") ui::drawTopBar(a);
    else if (piece_ == "transport") ui::drawTransport(a);
    else if (piece_ == "page") ui::drawPage(a, g_env, page_);
    else if (piece_ == "empty") ui::drawEmpty(a, s);
    else if (piece_ == "stats") ui::drawStats(s, av.x);
    else if (piece_ == "weather") ui::drawWeather(s, av.x, av.y);
    else if (piece_ == "car") ui::drawCar(s, av.x, av.y);
    else if (piece_ == "recent") ui::drawRecent(a, av.x, av.y);
    else if (piece_ == "map") ui::drawMap(a, s, av.x, av.y);
    else if (piece_ == "status") ui::drawCarStatus(s, av.x, av.y);
    else if (piece_ == "standings") ui::drawStandings(a, s, av.x, av.y);
    ImGui::End();
    ImGui::Render();

    // the mouse cursor ImGui asks for
    static const Qt::CursorShape shapes[] = {Qt::ArrowCursor,     Qt::IBeamCursor,   Qt::SizeAllCursor,
                                             Qt::SizeVerCursor,   Qt::SizeHorCursor, Qt::SizeBDiagCursor,
                                             Qt::SizeFDiagCursor, Qt::PointingHandCursor, Qt::ForbiddenCursor};
    const int mc = ImGui::GetMouseCursor();
    setCursor(QCursor(mc >= 0 && mc < 9 ? shapes[mc] : Qt::ArrowCursor));

    // copy the draw data out for the render thread
    const ImDrawData *dd = ImGui::GetDrawData();
    lists_.resize(size_t(dd->CmdListsCount));
    for (int n = 0; n < dd->CmdListsCount; n++) {
        const ImDrawList *l = dd->CmdLists[n];
        List &o = lists_[size_t(n)];
        o.vtx.assign(reinterpret_cast<const char *>(l->VtxBuffer.Data),
                     reinterpret_cast<const char *>(l->VtxBuffer.Data + l->VtxBuffer.Size));
        o.idx.assign(l->IdxBuffer.Data, l->IdxBuffer.Data + l->IdxBuffer.Size);
        o.cmds.clear();
        for (const ImDrawCmd &c : l->CmdBuffer) {
            if (c.UserCallback || !c.ElemCount) continue;
            Cmd k;
            k.clip[0] = std::max(0.f, c.ClipRect.x);
            k.clip[1] = std::max(0.f, c.ClipRect.y);
            k.clip[2] = std::min(W, c.ClipRect.z);
            k.clip[3] = std::min(H, c.ClipRect.w);
            if (k.clip[2] <= k.clip[0] || k.clip[3] <= k.clip[1]) continue;
            k.idxOffset = c.IdxOffset;
            k.idxCount = c.ElemCount;
            k.vtxOffset = c.VtxOffset;
            k.texture = intptr_t(c.GetTexID());
            o.cmds.push_back(k);
        }
    }
    dirty_ = true;
}

QSGNode *ImCanvas::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    QSGNode *root = old ? old : new QSGNode;
    if (!dirty_) return root;
    dirty_ = false;
    while (QSGNode *c = root->firstChild()) {
        root->removeChildNode(c);
        delete c;
    }
    QQuickWindow *win = window();
    if (!win) return root;
    WindowTextures &wt = g_textures[win];
    if (!wt.atlas) {
        wt.atlas = win->createTextureFromImage(g_atlasImage);
        wt.atlas->setFiltering(QSGTexture::Linear);
    }
    if (g_panelVersion != wt.panelVersion && !g_panelImage.isNull()) {
        delete wt.panel;
        wt.panel = win->createTextureFromImage(g_panelImage);
        wt.panel->setFiltering(QSGTexture::Linear);
        wt.panelVersion = g_panelVersion;
    }
    for (const List &l : lists_) {
        const int nv = int(l.vtx.size() / sizeof(ImDrawVert));
        for (const Cmd &c : l.cmds) {
            QSGTexture *tex = c.texture == TEX_PANEL ? wt.panel : wt.atlas;
            if (c.texture >= 100) {
                auto d = g_dyn.find(c.texture);
                if (d == g_dyn.end()) continue;
                auto &slot = wt.dyn[c.texture];
                if (!slot.first || slot.second != d->second.version) {
                    delete slot.first;
                    slot.first = win->createTextureFromImage(d->second.img);
                    slot.first->setFiltering(QSGTexture::Linear);
                    slot.second = d->second.version;
                }
                tex = slot.first;
            }
            if (!tex) continue;
            auto *clip = new QSGClipNode;
            clip->setIsRectangular(true);
            clip->setClipRect(QRectF(c.clip[0], c.clip[1], c.clip[2] - c.clip[0], c.clip[3] - c.clip[1]));
            auto *geo = new QSGGeometry(imAttributes(), nv, int(c.idxCount), QSGGeometry::UnsignedShortType);
            geo->setDrawingMode(QSGGeometry::DrawTriangles);
            std::memcpy(geo->vertexData(), l.vtx.data(), l.vtx.size());
            std::memcpy(geo->indexData(), l.idx.data() + c.idxOffset, c.idxCount * sizeof(unsigned short));
            auto *node = new QSGGeometryNode;
            node->setGeometry(geo);
            node->setFlag(QSGNode::OwnsGeometry);
            auto *mat = new ImMaterial;
            mat->tex = tex;
            node->setMaterial(mat);
            node->setFlag(QSGNode::OwnsMaterial);
            clip->appendChildNode(node);
            root->appendChildNode(clip);
        }
    }
    return root;
}

// ---- input ---------------------------------------------------------------------------------------------------------
namespace {
int imButton(Qt::MouseButton b) { return b == Qt::RightButton ? 1 : b == Qt::MiddleButton ? 2 : 0; }
} // namespace

void ImCanvas::mousePressEvent(QMouseEvent *e)
{
    if (!ctx_) return;
    forceActiveFocus(Qt::MouseFocusReason);
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(float(e->position().x()), float(e->position().y()));
    ImGui::GetIO().AddMouseButtonEvent(imButton(e->button()), true);
    e->accept();
    kick();
}

void ImCanvas::mouseReleaseEvent(QMouseEvent *e)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(float(e->position().x()), float(e->position().y()));
    ImGui::GetIO().AddMouseButtonEvent(imButton(e->button()), false);
    kick();
}

void ImCanvas::mouseDoubleClickEvent(QMouseEvent *e) { mousePressEvent(e); } // ImGui works out double clicks itself

void ImCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(float(e->position().x()), float(e->position().y()));
    kick();
}

void ImCanvas::hoverMoveEvent(QHoverEvent *e)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(float(e->position().x()), float(e->position().y()));
    kick();
}

void ImCanvas::hoverLeaveEvent(QHoverEvent *)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    kick();
}

void ImCanvas::wheelEvent(QWheelEvent *e)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGui::GetIO().AddMouseWheelEvent(float(e->angleDelta().x()) / 120.f, float(e->angleDelta().y()) / 120.f);
    e->accept();
    kick();
}

void ImCanvas::key(QKeyEvent *e, bool down)
{
    if (!ctx_) return;
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, e->modifiers() & Qt::ControlModifier);
    io.AddKeyEvent(ImGuiMod_Shift, e->modifiers() & Qt::ShiftModifier);
    io.AddKeyEvent(ImGuiMod_Alt, e->modifiers() & Qt::AltModifier);
    ImGuiKey k = ImGuiKey_None;
    switch (e->key()) {
    case Qt::Key_Escape: k = ImGuiKey_Escape; break;
    case Qt::Key_Return: k = ImGuiKey_Enter; break;
    case Qt::Key_Enter: k = ImGuiKey_KeypadEnter; break;
    case Qt::Key_Backspace: k = ImGuiKey_Backspace; break;
    case Qt::Key_Delete: k = ImGuiKey_Delete; break;
    case Qt::Key_Tab: k = ImGuiKey_Tab; break;
    case Qt::Key_Left: k = ImGuiKey_LeftArrow; break;
    case Qt::Key_Right: k = ImGuiKey_RightArrow; break;
    case Qt::Key_Up: k = ImGuiKey_UpArrow; break;
    case Qt::Key_Down: k = ImGuiKey_DownArrow; break;
    case Qt::Key_Home: k = ImGuiKey_Home; break;
    case Qt::Key_End: k = ImGuiKey_End; break;
    case Qt::Key_A: k = ImGuiKey_A; break;
    case Qt::Key_C: k = ImGuiKey_C; break;
    case Qt::Key_V: k = ImGuiKey_V; break;
    case Qt::Key_X: k = ImGuiKey_X; break;
    case Qt::Key_Z: k = ImGuiKey_Z; break;
    case Qt::Key_Space: k = ImGuiKey_Space; break;
    default: break;
    }
    if (k != ImGuiKey_None) io.AddKeyEvent(k, down);
    if (down && !(e->modifiers() & Qt::ControlModifier)) {
        const QByteArray t = e->text().toUtf8();
        if (!t.isEmpty() && uint8_t(t[0]) >= 0x20 && t[0] != 0x7f) io.AddInputCharactersUTF8(t.constData());
    }
    e->accept();
    kick();
}

void ImCanvas::keyPressEvent(QKeyEvent *e) { key(e, true); }
void ImCanvas::keyReleaseEvent(QKeyEvent *e) { key(e, false); }

void ImCanvas::focusOutEvent(QFocusEvent *e)
{
    if (ctx_) {
        ImGui::SetCurrentContext(ctx_);
        ImGui::GetIO().AddFocusEvent(false);
    }
    QQuickItem::focusOutEvent(e);
}
