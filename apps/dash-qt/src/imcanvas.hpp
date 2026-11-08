// QML item `ImCanvas` (module F1Native): one piece of the original ImGui desktop UI (ui.cpp), drawn with its own
// ImGui context and rendered by the Qt scene graph (the ImGui vertices go straight to the GPU with a small shader).
// The QML pages place and size the pieces; what is inside each looks and works exactly as before.
//
//   piece: "sidebar", "topbar", "transport", "page" (with pageIndex), "empty", "stats", "car", "recent", "map",
//          "status", "standings"
#pragma once

#include <QQuickItem>

#include <QImage>

#include <chrono>
#include <string>
#include <vector>

struct ImGuiContext;
namespace engine { class Engine; }

class ImCanvas : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QString piece READ piece WRITE setPiece NOTIFY changed)
    Q_PROPERTY(int pageIndex READ pageIndex WRITE setPageIndex NOTIFY changed)
    Q_PROPERTY(QSizeF padding READ padding WRITE setPadding NOTIFY changed) // ImGui window padding inside the item
public:
    explicit ImCanvas(QQuickItem *parent = nullptr);
    ~ImCanvas() override;

    // once, before QML loads: the engine, and the scale the fonts are rasterised at (window scale x device pixels)
    static void setup(engine::Engine *e, float fontBake);
    static void tickAll(); // ~30 Hz: redraw every canvas
    // a texture the ImGui drawing can use (premultiplied RGBA), one per `slot`, replaced when called again
    static void *dynamicTexture(const std::string &slot, const QImage &img);

    QString piece() const { return piece_; }
    void setPiece(const QString &p);
    int pageIndex() const { return page_; }
    void setPageIndex(int p);
    QSizeF padding() const { return pad_; }
    void setPadding(QSizeF p);

signals:
    void changed();

protected:
    void updatePolish() override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void hoverMoveEvent(QHoverEvent *e) override;
    void hoverLeaveEvent(QHoverEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void kick(); // input arrived: draw a frame soon
    void frame(); // one ImGui frame into the buffers below
    void key(QKeyEvent *e, bool down);

    QString piece_;
    int page_ = 0;
    QSizeF pad_{0, 0};
    ImGuiContext *ctx_ = nullptr;
    std::chrono::steady_clock::time_point last_;

    // the last frame, copied out of ImGui for the render thread
    struct Cmd {
        float clip[4];
        unsigned idxOffset, idxCount, vtxOffset;
        intptr_t texture; // ImTextureID: 1 font atlas, 2 HMD panel texture, 100+ dynamic
    };
    struct List {
        std::vector<char> vtx; // ImDrawVert
        std::vector<unsigned short> idx;
        std::vector<Cmd> cmds;
    };
    std::vector<List> lists_;
    bool dirty_ = false;
};
