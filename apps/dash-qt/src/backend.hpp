// What QML sees: the `Race` singleton (module F1Data). Same names as the designer mock
// (apps/dash-qt/imports/F1Data/Race.qml), so a page designed with the mock shows the real data unchanged.
//
// Everything is refreshed together (one `updated` signal) at ~30 Hz from the engine's snapshot; values arrive
// formatted (strings) and coloured (QColor) so the QML only lays them out.
#pragma once

#include "engine.hpp"

#include <QAbstractListModel>
#include <QColor>
#include <QObject>
#include <QVariantList>

class StandingsModel : public QAbstractListModel {
    Q_OBJECT
public:
    struct Row {
        int pos = 0;
        QString code, gap, last, best;
        QColor team, lastColor, bestColor, tyreColor;
        QString tyre;
        int age = 0;
        bool inPit = false, you = false, lapped = false;
    };
    enum Role { Pos = Qt::UserRole + 1, Code, Team, Gap, Last, LastColor, Best, BestColor, Tyre, TyreColor, Age, InPit, You, Lapped };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex & = {}) const override { return int(rows_.size()); }
    QVariant data(const QModelIndex &i, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void set(std::vector<Row> rows);

private:
    std::vector<Row> rows_;
};

class Backend : public QObject {
    Q_OBJECT
    // source
    Q_PROPERTY(bool hasData READ hasData NOTIFY updated)
    Q_PROPERTY(int source READ source NOTIFY updated) // 0 live, 1 virtual, 2 replay
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY updated)
    Q_PROPERTY(QString status READ status NOTIFY updated)
    Q_PROPERTY(int port READ port NOTIFY updated)
    Q_PROPERTY(double packetRate READ packetRate NOTIFY updated)
    Q_PROPERTY(bool recording READ recording NOTIFY updated)
    Q_PROPERTY(QString vrStatus READ vrStatus NOTIFY updated)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(int startPage READ startPage CONSTANT)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY updated)          // the sidebar's page (ui.cpp keeps it)
    Q_PROPERTY(bool mapFull READ mapFull WRITE setMapFull NOTIFY updated) // dashboard as a full-screen map
    Q_PROPERTY(bool transport READ transport NOTIFY updated)             // replay controls at the bottom
    // session
    Q_PROPERTY(QString trackName MEMBER trackName_ NOTIFY updated)
    Q_PROPERTY(QString sessionName MEMBER sessionName_ NOTIFY updated)
    Q_PROPERTY(int lap MEMBER lap_ NOTIFY updated)
    Q_PROPERTY(int totalLaps MEMBER totalLaps_ NOTIFY updated)
    Q_PROPERTY(int position MEMBER position_ NOTIFY updated)
    Q_PROPERTY(int numCars MEMBER numCars_ NOTIFY updated)
    // lap times
    Q_PROPERTY(QString currentLap MEMBER currentLap_ NOTIFY updated)
    Q_PROPERTY(bool currentInvalid MEMBER currentInvalid_ NOTIFY updated)
    Q_PROPERTY(QString lastLap MEMBER lastLap_ NOTIFY updated)
    Q_PROPERTY(QColor lastLapColor MEMBER lastLapColor_ NOTIFY updated)
    Q_PROPERTY(QString bestLap MEMBER bestLap_ NOTIFY updated)
    Q_PROPERTY(QColor bestLapColor MEMBER bestLapColor_ NOTIFY updated)
    Q_PROPERTY(QString sessionBest MEMBER sessionBest_ NOTIFY updated)
    Q_PROPERTY(QVariantList sectors MEMBER sectors_ NOTIFY updated) // [{label, time, color, done}]
    // car
    Q_PROPERTY(int speed MEMBER speed_ NOTIFY updated)
    Q_PROPERTY(QString gear MEMBER gear_ NOTIFY updated)
    Q_PROPERTY(int rpm MEMBER rpm_ NOTIFY updated)
    Q_PROPERTY(double revLights MEMBER revLights_ NOTIFY updated) // 0..1
    Q_PROPERTY(double throttle MEMBER throttle_ NOTIFY updated)
    Q_PROPERTY(double brake MEMBER brake_ NOTIFY updated)
    Q_PROPERTY(double steer MEMBER steer_ NOTIFY updated) // -1..1
    Q_PROPERTY(double ersStore MEMBER ers_ NOTIFY updated) // 0..1
    Q_PROPERTY(bool ersFault MEMBER ersFault_ NOTIFY updated)
    Q_PROPERTY(QString ersMode MEMBER ersMode_ NOTIFY updated)
    Q_PROPERTY(int drsState MEMBER drsState_ NOTIFY updated) // 0 off, 1 allowed, 2 open, 3 fault
    Q_PROPERTY(QVariantMap fuel MEMBER fuel_ NOTIFY updated) // kg, laps, now, per100, thisLap, lastLap, avg, left
    Q_PROPERTY(QVariantList tyres MEMBER tyres_ NOTIFY updated) // FL FR RL RR: {inner, surface, wear, brake, innerColor, wearColor, brakeColor}
    Q_PROPERTY(QString compound MEMBER compound_ NOTIFY updated)
    Q_PROPERTY(QColor compoundColor MEMBER compoundColor_ NOTIFY updated)
    Q_PROPERTY(int tyreAge MEMBER tyreAge_ NOTIFY updated)
    // standings
    Q_PROPERTY(QObject *standings READ standings CONSTANT)
    Q_PROPERTY(int gapMode READ gapMode WRITE setGapMode NOTIFY updated) // 0 to me, 1 interval, 2 to leader

public:
    Backend(engine::Engine &e, int startPage, QObject *parent = nullptr);

    void refresh(); // from the engine's snapshot

    bool hasData() const { return e_.snap.hasData; }
    int source() const { return int(e_.a.source()); }
    QString sourceName() const;
    QString status() const { return QString::fromStdString(e_.a.status()); }
    int port() const { return e_.a.cfg.port; }
    double packetRate() const { return e_.a.packetRate(); }
    bool recording() const { return e_.a.recording(); }
    QString vrStatus() const { return QString::fromStdString(e_.vrStatus); }
    QString version() const;
    int startPage() const { return startPage_; }
    QObject *standings() { return &standings_; }
    int gapMode() const { return e_.a.cfg.gapMode; }
    int page() const;
    void setPage(int p);
    bool mapFull() const;
    void setMapFull(bool on);
    bool transport() const;
    void setGapMode(int m);

    Q_INVOKABLE void useLive();
    Q_INVOKABLE void useSim();
    Q_INVOKABLE void openRecording(); // file dialog
    Q_INVOKABLE void toggleRecording();
    Q_INVOKABLE void launchSteamVr() { e_.startVr(true); }
    Q_INVOKABLE void reconnectSteamVr() { e_.startVr(false); }

signals:
    void updated();

private:
    engine::Engine &e_;
    int startPage_;
    StandingsModel standings_;

    QString trackName_, sessionName_;
    int lap_ = 0, totalLaps_ = 0, position_ = 0, numCars_ = 0;
    QString currentLap_, lastLap_, bestLap_, sessionBest_;
    bool currentInvalid_ = false;
    QColor lastLapColor_, bestLapColor_;
    QVariantList sectors_;
    int speed_ = 0, rpm_ = 0, drsState_ = 0, tyreAge_ = 0;
    QString gear_ = "N", ersMode_, compound_;
    double revLights_ = 0, throttle_ = 0, brake_ = 0, steer_ = 0, ers_ = 0;
    bool ersFault_ = false;
    QVariantMap fuel_;
    QVariantList tyres_;
    QColor compoundColor_;
};
