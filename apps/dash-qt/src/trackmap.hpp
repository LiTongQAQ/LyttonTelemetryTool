// QML item `TrackMap` (module F1Native): the circuit and the cars on it, painted with QPainter from the engine's
// snapshot and the aligned track outline (app::App::trackGeo). Repaints on Backend::updated.
#pragma once

#include <QQuickPaintedItem>

namespace engine { class Engine; }

class TrackMap : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged) // 1 = whole circuit, >1 follows the player
public:
    explicit TrackMap(QQuickItem *parent = nullptr);
    void paint(QPainter *p) override;
    double zoom() const { return zoom_; }
    void setZoom(double z);

    static engine::Engine *engine; // set by main()

signals:
    void zoomChanged();

private:
    double zoom_ = 1;
};
