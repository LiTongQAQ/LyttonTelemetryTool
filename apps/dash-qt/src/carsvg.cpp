// The car top view from Track N Race's original SVG (third_party/track-n-race/car/f1-car-wireframe.svg, NoGoat,
// GPL-3.0), rendered by QtSvg instead of carwire's polylines: carwire::draw() asks for an image with the part
// colours, and gets a texture for whichever ImGui context is drawing (a desktop canvas or the HMD panels).
//
// The SVG is meant to be recoloured by element id (see its header comment): each part's .solid shapes get the fill,
// the line and tube colours are rewritten, and the #car group's rotation is dropped so the drawing stays nose up in
// the frame carwire.inc uses (viewBox 2 -13 346 927), which keeps carwire::box() matching.
#include "carsvg.hpp"

#include "carwire.hpp"
#include "engine.hpp"
#include "imcanvas.hpp"

#include <QFile>
#include <QImage>
#include <QPainter>
#include <QRegularExpression>
#include <QSvgRenderer>

#include <cmath>
#include <chrono>
#include <map>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

engine::Engine *g_eng = nullptr;
QString g_template; // the SVG, nose up, with the parts' colours as placeholders

const char *PART_IDS[carwire::PARTS] = {"body", "cockpit", "suspension", "z-fw-l", "z-fw-r", "z-floor", "z-diff",
                                        "z-sp-l", "z-sp-r", "z-rw", "t-fl", "t-fr", "t-rl", "t-rr"};

QString hex(ImU32 c)
{
    return QString::asprintf("#%02x%02x%02x", unsigned(c & 0xFF), unsigned(c >> 8 & 0xFF), unsigned(c >> 16 & 0xFF));
}

// `over` (with its alpha) on top of the opaque `bg`
ImU32 blend(ImU32 bg, ImU32 over)
{
    const float a = float(over >> 24 & 0xFF) / 255.f;
    auto ch = [&](int sh) { return unsigned(std::lround(float(bg >> sh & 0xFF) * (1 - a) + float(over >> sh & 0xFF) * a)); };
    return IM_COL32(ch(0), ch(8), ch(16), 255);
}

bool loadTemplate()
{
    QFile f(QStringLiteral(":/car/f1-car-wireframe.svg"));
    if (!f.open(QIODevice::ReadOnly)) return false;
    QString s = QString::fromUtf8(f.readAll());
    s.replace(QRegularExpression(QStringLiteral("<svg ([^>]*)viewBox=\"[^\"]*\" width=\"[^\"]*\" height=\"[^\"]*\"")),
              QStringLiteral("<svg \\1viewBox=\"2 -13 346 927\" width=\"346\" height=\"927\""));
    s.replace(QRegularExpression(QStringLiteral("(<g id=\"car\")\\s+transform=\"[^\"]*\"")), QStringLiteral("\\1"));
    // each part group's solid fill -> a placeholder per part
    for (int i = 0; i < carwire::PARTS; i++) {
        const QString open = QStringLiteral("<g id=\"%1\"").arg(PART_IDS[i]);
        const int a = s.indexOf(open);
        if (a < 0) continue;
        const int b = s.indexOf(QStringLiteral("</g>"), a);
        if (b < 0) continue;
        QString g = s.mid(a, b - a);
        g.replace(QStringLiteral("fill=\"#11141f\""), QStringLiteral("fill=\"@F%1@\"").arg(i));
        s.replace(a, b - a, g);
    }
    s.replace(QStringLiteral("fill=\"#11141f\""), QStringLiteral("fill=\"@BG@\""));
    s.replace(QStringLiteral("#8a93c4"), QStringLiteral("@LINE@"));
    s.replace(QStringLiteral("#4a5175"), QStringLiteral("@TUBE@"));
    g_template = s;
    return true;
}

QImage rasterize(const carwire::Colours &c, int w, int h)
{
    QString s = g_template;
    for (int i = 0; i < carwire::PARTS; i++)
        s.replace(QStringLiteral("@F%1@").arg(i), hex(c.fill[i] ? blend(c.bg, c.fill[i]) : c.bg));
    s.replace(QStringLiteral("@BG@"), hex(c.bg));
    s.replace(QStringLiteral("@LINE@"), hex(c.line));
    s.replace(QStringLiteral("@TUBE@"), hex(c.tube));
    QSvgRenderer r(s.toUtf8());
    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    r.render(&p, QRectF(0, 0, w, h));
    return img;
}

// one image per target and size, re-rendered only when the colours change
struct Cached {
    std::string key;
    ImTextureID tex = 0;
    std::chrono::steady_clock::time_point at; // last rendered
};
std::map<std::string, Cached> g_desk; // GUI thread only

// Lock-free hand-over between one writer and one reader: three slots, the writer fills its own and swaps it with
// the "latest" slot, the reader swaps the latest one into its hand when there is a newer one. Writer and reader never
// touch the same slot, so a half-written image is never read and neither side ever waits for the other; when the
// writer is slow the reader keeps what it has (the previous image), when it is fast the older images are skipped.
template <class T> class TripleBuffer {
public:
    T &back() { return slot_[back_]; }       // writer: fill this one, then publish()
    void publish() { back_ = latest_.exchange(back_ | FRESH) & IDX; }
    bool take()                              // reader: true when front() now holds a newer value
    {
        if (!(latest_.load() & FRESH)) return false;
        front_ = latest_.exchange(front_) & IDX;
        return true;
    }
    T &front() { return slot_[front_]; }

private:
    static constexpr unsigned FRESH = 4, IDX = 3;
    T slot_[3];
    unsigned back_ = 0, front_ = 1;
    std::atomic<unsigned> latest_{2};
};

// The HMD car is rendered on a worker thread: QtSvg at the HMD texture's size takes tens of ms, which inside the
// panel frame made every other panel (lap timer, pedals) stutter. The panel frame never waits for it: it draws with
// the newest finished image (or the previous one while the next is still being rendered) and the other panels as
// usual.
struct CarJob {
    carwire::Colours colours{};
    int w = 0, h = 0;
    std::string key;
};
struct CarImage {
    QImage img;
    std::string key;
};
struct HmdCar {
    TripleBuffer<CarJob> jobs;     // panel thread -> worker: the colours wanted now
    TripleBuffer<CarImage> images; // worker -> panel thread: finished images
    std::atomic<bool> stop{false};
    std::mutex wakeM; // only for the worker's sleep; the panel thread never takes it
    std::condition_variable wake;
    std::thread worker;
    // panel thread only
    std::string slot, shownKey, askedKey;
    ImTextureID tex = 0;
    std::chrono::steady_clock::time_point at;
} g_hmd;

void hmdWorker()
{
    auto &H = g_hmd;
    while (!H.stop) {
        if (!H.jobs.take()) { // nothing new asked: sleep (a short timeout covers a missed wake-up)
            std::unique_lock<std::mutex> l(H.wakeM);
            H.wake.wait_for(l, std::chrono::milliseconds(50));
            continue;
        }
        const CarJob &j = H.jobs.front();
        CarImage &out = H.images.back();
        out.img = rasterize(j.colours, j.w, j.h).convertToFormat(QImage::Format_RGBA8888); // DX11 blends straight alpha
        out.key = j.key;
        H.images.publish();
    }
}

ImTextureID hmdImage(const carwire::Colours &c, int pw, int ph)
{
    auto &H = g_hmd;
    const std::string slot = "hmd " + std::to_string(pw) + "x" + std::to_string(ph);
    std::string key(reinterpret_cast<const char *>(&c), sizeof c);
    key += slot;
    if (H.images.take()) { // a newer finished image: upload it (fast)
        const CarImage &ci = H.images.front();
        if (!ci.img.isNull()) {
            H.tex = (ImTextureID)g_eng->uploadTexture(slot, ci.img.constBits(), ci.img.width(), ci.img.height());
            H.shownKey = ci.key;
            H.slot = slot;
        }
    }
    // ask for new colours (temperatures, damage change slowly: 4 times a second are plenty)
    const auto now = std::chrono::steady_clock::now();
    if (!H.stop && key != H.shownKey && key != H.askedKey && (!H.tex || now - H.at >= std::chrono::milliseconds(250))) {
        H.at = now;
        CarJob &j = H.jobs.back();
        j.colours = c;
        j.w = pw;
        j.h = ph;
        j.key = H.askedKey = key;
        H.jobs.publish();
        if (!H.worker.joinable()) H.worker = std::thread(hmdWorker);
        H.wake.notify_one();
    }
    return H.slot == slot ? H.tex : 0; // the previous image until the new one is ready
}

ImTextureID image(const carwire::Colours &c, float w, float h)
{
    if (!g_eng || g_template.isEmpty() || w < 4 || h < 4) return 0;
    const bool hmd = g_eng->inPanelFrame();
    // rendered sharper than its layout size: the desktop is scaled up by the window, the HMD texture by texScale
    const float over = hmd ? g_eng->texScale : 2.f;
    const int pw = int(std::ceil(w * over)), ph = int(std::ceil(h * over));
    if (hmd) return hmdImage(c, pw, ph);
    const std::string slot = "desk " + std::to_string(pw) + "x" + std::to_string(ph);
    std::string key(reinterpret_cast<const char *>(&c), sizeof c);
    Cached &e = g_desk[slot];
    if (e.tex && e.key == key) return e.tex;
    e.at = std::chrono::steady_clock::now();
    e.tex = (ImTextureID)ImCanvas::dynamicTexture(slot, rasterize(c, pw, ph).convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    e.key = key;
    return e.tex;
}

} // namespace

namespace carsvg {

bool install(engine::Engine *e)
{
    g_eng = e;
    if (!loadTemplate()) return false;
    carwire::setImageRenderer(&image);
    return true;
}

void shutdown()
{
    g_hmd.stop = true;
    g_hmd.wake.notify_one();
    if (g_hmd.worker.joinable()) g_hmd.worker.join();
}

} // namespace carsvg
