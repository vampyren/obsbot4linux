#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <algorithm>

QString Settings::configPath() {
    const QByteArray env = qgetenv("OBSBOT4LINUX_CONFIG");
    if (!env.isEmpty())
        return QString::fromLocal8Bit(env);
    // Per-user XDG config so the app works when installed (e.g. from an AppImage),
    // not just when run from the repo:  ~/.config/obsbot4linux/…json
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/obsbot4linux/obsbot4linux.json");
}

static QJsonObject presetToJson(const PresetData &p) {
    QJsonObject o;
    o["set"] = p.set;
    o["name"] = p.name;
    o["pan"] = p.pan;
    o["tilt"] = p.tilt;
    o["zoom"] = p.zoom;
    o["fov"] = p.fov;
    return o;
}

static PresetData presetFromJson(const QJsonObject &o) {
    PresetData p;
    p.set = o.value("set").toBool(false);
    p.name = o.value("name").toString();
    p.pan = o.value("pan").toDouble(0.0);
    p.tilt = o.value("tilt").toDouble(0.0);
    p.zoom = o.value("zoom").toDouble(1.0);
    p.fov = o.value("fov").toInt(0);
    return p;
}

QJsonObject Settings::toJson(const AppSettings &s) {
    QJsonObject o;
    o["schema"] = 1;
    o["moveStepDeg"] = s.moveStepDeg;
    o["speedMode"] = s.speedMode;
    o["fovIndex"] = s.fovIndex;
    o["startupPreset"] = s.startupPreset;
    o["aiReturnPreset"] = s.aiReturnPreset;
    o["previewResIndex"] = s.previewResIndex;
    o["sleepOnExit"] = s.sleepOnExit;

    QJsonArray presets;
    for (const auto &p : s.presets)
        presets.append(presetToJson(p));
    o["presets"] = presets;

    QJsonObject img;
    img["brightness"] = s.brightness;
    img["contrast"] = s.contrast;
    img["saturation"] = s.saturation;
    img["sharpen"] = s.sharpen;
    img["wbMode"] = s.wbMode;
    img["wbTemp"] = s.wbTemp;
    img["exposure"] = s.exposure;
    img["hdr"] = s.hdr;
    o["image"] = img;

    QJsonObject trk;
    trk["framing"] = s.trackFraming;
    trk["speed"] = s.trackSpeed;
    trk["zone"] = s.trackZone;
    trk["sensitivity"] = s.sensitivity;
    trk["gesture"] = s.gesture;
    trk["gestureLowTraffic"] = s.gestureLowTraffic;
    o["tracking"] = trk;
    return o;
}

AppSettings Settings::fromJson(const QJsonObject &o) {
    AppSettings s;  // starts at defaults; every field falls back to its default
    s.moveStepDeg = o.value("moveStepDeg").toInt(s.moveStepDeg);
    s.speedMode = o.value("speedMode").toInt(s.speedMode);
    // Clamp indices that are used to subscript fixed-size tables (kFovLabels[3])
    // — a hand-edited or downgraded config must never index out of bounds (UB).
    s.fovIndex = std::clamp(o.value("fovIndex").toInt(s.fovIndex), 0, 2);
    s.startupPreset = std::clamp(o.value("startupPreset").toInt(s.startupPreset), 0, 3);
    s.aiReturnPreset = std::clamp(o.value("aiReturnPreset").toInt(s.aiReturnPreset), 0, 3);
    s.previewResIndex = std::clamp(o.value("previewResIndex").toInt(s.previewResIndex), 0, 3);
    s.sleepOnExit = o.value("sleepOnExit").toBool(s.sleepOnExit);

    const QJsonArray presets = o.value("presets").toArray();
    for (int i = 0; i < 3 && i < presets.size(); ++i)
        s.presets[i] = presetFromJson(presets[i].toObject());

    const QJsonObject img = o.value("image").toObject();
    s.brightness = img.value("brightness").toInt(s.brightness);
    s.contrast = img.value("contrast").toInt(s.contrast);
    s.saturation = img.value("saturation").toInt(s.saturation);
    s.sharpen = img.value("sharpen").toInt(s.sharpen);
    s.wbMode = img.value("wbMode").toInt(s.wbMode);
    s.wbTemp = img.value("wbTemp").toInt(s.wbTemp);
    s.exposure = img.value("exposure").toInt(s.exposure);
    s.hdr = img.value("hdr").toBool(s.hdr);

    const QJsonObject trk = o.value("tracking").toObject();
    s.trackFraming = trk.value("framing").toInt(s.trackFraming);
    s.trackSpeed = trk.value("speed").toInt(s.trackSpeed);
    s.trackZone = trk.value("zone").toInt(s.trackZone);
    s.sensitivity = trk.value("sensitivity").toInt(s.sensitivity);
    s.gesture = trk.value("gesture").toBool(s.gesture);
    s.gestureLowTraffic = trk.value("gestureLowTraffic").toBool(s.gestureLowTraffic);
    return s;
}

AppSettings Settings::load() {
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return AppSettings{};
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return AppSettings{};
    return fromJson(doc.object());
}

bool Settings::save(const AppSettings &s) {
    const QString path = configPath();
    const QFileInfo fi(path);
    if (!fi.dir().exists() && !QDir().mkpath(fi.absolutePath()))
        return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    const QJsonDocument doc(toJson(s));
    const QByteArray bytes = doc.toJson(QJsonDocument::Indented);
    return f.write(bytes) == bytes.size();
}
