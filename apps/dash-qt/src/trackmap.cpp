#include "trackmap.hpp"

#include "engine.hpp"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

engine::Engine *TrackMap::engine = nullptr;

namespace {
QColor teamColour(uint8_t id)
{
    static const QColor c[] = {{39, 244, 210}, {232, 0, 32},   {54, 113, 198}, {100, 196, 255}, {34, 153, 113},
                               {255, 135, 188}, {102, 146, 255}, {182, 186, 189}, {255, 128, 0},  {82, 226, 82}};
    return id < 10 ? c[id] : QColor(120, 120, 120);
}
} // namespace

TrackMap::TrackMap(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setOpaquePainting(false);
}

void TrackMap::setZoom(double z)
{
    z = std::clamp(z, 1.0 / 1.5, 8.0);
    if (z == zoom_) return;
    zoom_ = z;
    emit zoomChanged();
    update();
}

void TrackMap::paint(QPainter *p)
{
    if (!engine) return;
    const auto &s = engine->snap;
    const auto &geo = engine->a.trackGeo;
    const QColor dim(135, 144, 157);
    p->setRenderHint(QPainter::Antialiasing);
    if (!geo.ok || geo.cx.size() < 3) {
        p->setPen(dim);
        p->drawText(boundingRect(), Qt::AlignCenter,
                    s.hasData ? "Waiting for the track outline..." : "No telemetry");
        return;
    }
    float x0 = 1e9f, x1 = -1e9f, z0 = 1e9f, z1 = -1e9f;
    for (size_t i = 0; i < geo.lx.size(); i++)
        for (auto [x, z] : {std::pair{geo.lx[i], geo.lz[i]}, std::pair{geo.rx[i], geo.rz[i]}}) {
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            z0 = std::min(z0, z); z1 = std::max(z1, z);
        }
    const double W = width(), H = height(), pad = 26;
    const double k0 = std::min((W - 2 * pad) / std::max(1.f, x1 - x0), (H - 2 * pad) / std::max(1.f, z1 - z0));
    const timing::Car *me = s.playerIdx < f1tel::MAX_CARS ? &s.cars[s.playerIdx] : nullptr;
    const bool follow = zoom_ > 1.01 && me && me->hasPosition;
    const double k = k0 * zoom_;
    const double cx = follow ? me->worldX : (x0 + x1) / 2, cz = follow ? me->worldZ : (z0 + z1) / 2;
    auto map = [&](float x, float z) { return QPointF(W / 2 + (x - cx) * k, H / 2 + (cz - z) * k); };

    // asphalt between the edges, segment by segment (skip across the start line)
    const size_t n = geo.cx.size();
    QPainterPath road;
    for (size_t i = 0; i < n; i++) {
        const size_t j = (i + 1) % n;
        if (!geo.dist.empty() && std::fabs(geo.dist[j] - geo.dist[i]) > 50 && j != 0) continue;
        QPolygonF quad;
        quad << map(geo.lx[i], geo.lz[i]) << map(geo.lx[j], geo.lz[j]) << map(geo.rx[j], geo.rz[j]) << map(geo.rx[i], geo.rz[i]);
        road.addPolygon(quad);
    }
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(58, 64, 76));
    p->drawPath(road.simplified());

    // DRS zones: light green over the surface
    for (const auto &zn : geo.zones) {
        if (zn.kind != 'D') continue;
        QPainterPath zp;
        for (size_t i = 0; i < n; i++) {
            const size_t j = (i + 1) % n;
            const float d = geo.dist.empty() ? 0 : geo.dist[i];
            const bool in = zn.d0 <= zn.d1 ? d >= zn.d0 && d <= zn.d1 : d >= zn.d0 || d <= zn.d1;
            if (!in) continue;
            QPolygonF quad;
            quad << map(geo.lx[i], geo.lz[i]) << map(geo.lx[j], geo.lz[j]) << map(geo.rx[j], geo.rz[j]) << map(geo.rx[i], geo.rz[i]);
            zp.addPolygon(quad);
        }
        p->setBrush(QColor(120, 230, 140, 70));
        p->drawPath(zp.simplified());
    }

    // edges
    QPen edge(QColor(150, 158, 170), 1.2);
    p->setPen(edge);
    p->setBrush(Qt::NoBrush);
    for (const auto *side : {&geo.lx, &geo.rx}) {
        const auto &xs = *side;
        const auto &zs = side == &geo.lx ? geo.lz : geo.rz;
        QPolygonF poly;
        for (size_t i = 0; i < n; i++) poly << map(xs[i], zs[i]);
        poly << poly.front();
        p->drawPolyline(poly);
    }

    // cars: team colour dot, code beside it; yours bigger with a white ring; pit lane cars hidden
    QFont f("Titillium Web", 9, QFont::DemiBold);
    p->setFont(f);
    for (int i : s.order()) {
        const auto &c = s.cars[size_t(i)];
        if (!c.hasPosition || c.pitStatus) continue;
        const QPointF pt = map(c.worldX, c.worldZ);
        const bool mine = i == s.playerIdx;
        const double r = mine ? 7 : 5;
        p->setPen(mine ? QPen(Qt::white, 2) : QPen(QColor(0, 0, 0, 160), 1));
        p->setBrush(teamColour(c.teamId));
        p->drawEllipse(pt, r, r);
        p->setPen(mine ? Qt::white : QColor(220, 224, 230));
        const std::string code = c.ai || c.you ? timing::driverCode(c) : timing::playerCode(c.name);
        p->drawText(pt + QPointF(r + 3, -r), QString::fromStdString(code));
    }
}
