#pragma once
#include <QVariant>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <optional>
#include <cmath>

// Test-only demonstration data for rendering; never linked into ucc-gui.
inline std::optional<QVariant> visualFixtureReply(const QString &method) {
  const QString profiles=R"([{"id":"quiet","name":"Quiet","description":"Low noise"},{"id":"balanced","name":"Balanced","description":"Everyday work and browsing","odmPowerLimits":{"tdpValues":[65,90,110]}},{"id":"performance","name":"Performance","description":"Maximum performance"}])";
  const QString fan=R"({"id":"balanced-cooling","name":"Balanced cooling","tableCPU":[{"temp":30,"speed":0},{"temp":40,"speed":10},{"temp":50,"speed":25},{"temp":60,"speed":40},{"temp":70,"speed":55},{"temp":80,"speed":75},{"temp":90,"speed":90},{"temp":100,"speed":100}],"tableGPU":[{"temp":30,"speed":0},{"temp":40,"speed":10},{"temp":50,"speed":25},{"temp":60,"speed":40},{"temp":70,"speed":55},{"temp":80,"speed":75},{"temp":90,"speed":90},{"temp":100,"speed":100}]})";
  if(method=="GetSystemInfoJSON") return QString(R"({"laptopModel":"MECHREVO YAOSHI Series-X6AR55xY","cpuModel":"Intel Core Ultra 9 275HX","dGpuModel":"NVIDIA GeForce RTX 5080 Laptop GPU"})");
  if(method=="GetDefaultProfilesJSON") return profiles;
  if(method=="GetActiveProfileJSON") return QString(R"({"id":"balanced","name":"Balanced"})");
  if(method=="GetPowerState") return QString("mains");
  if(method=="GetWaterCoolerSupported" || method=="GetKeyboardBacklightControlSupported" || method=="GetWaterCoolerConnected" || method=="GetWebcamSWStatus" || method=="IsWaterCoolerEnabled") return true;
  if(method=="GetKeyboardBacklightCapabilitiesJSON") return QString(R"({"zones":128,"maxBrightness":255,"maxRed":255,"maxGreen":255,"maxBlue":255})");
  if(method=="GetKeyboardBacklightStatesJSON") {
    QJsonArray keys;for(int i=0;i<128;++i) keys.append(QJsonObject{{"mode",0},{"brightness",128},{"red",239},{"green",0},{"blue",216}});
    return QString::fromUtf8(QJsonDocument(keys).toJson(QJsonDocument::Compact));
  }
  if(method=="GetCustomKeyboardProfilesJSON") return QString(R"([{"id":"keyboard","name":"Keyboard","brightness":128}])");
  if(method=="GetFanProfileNames") return QString(R"([{"id":"balanced-cooling","name":"Balanced cooling"}])");
  if(method=="GetFanProfile") return fan;
  if(method=="ODMPowerLimitsJSON") return QString("[110,110,110]");
  if(method=="GetCpuCoreCount") return 24;
  if(method=="GetAvailableGovernors") return QString(R"(["powersave","performance"])");
  if(method=="GetAvailableEPPs") return QString(R"(["balance_performance","balance_power","performance"])");
  if(method=="GetCpuFrequencyMHz") return 1700;
  if(method=="GetDisplayBrightness") return 75;
  if(method=="GetCpuPowerValuesJSON") return QString(R"({"powerDraw":38.4})");
  if(method=="GetDGpuInfoValuesJSON") return QString(R"({"temp":51,"coreFrequency":1900,"powerDraw":73.5,"computeUtilPct":8,"memoryUtilPct":4,"currentPstate":4,"grClockOffsetMHz":0,"memClockOffsetMHz":0})");
  if(method.startsWith("GetFanData")) return QVariantMap{{"temp",QVariantMap{{"timestamp",QDateTime::currentMSecsSinceEpoch()},{"data",52}}},{"speed",QVariantMap{{"timestamp",QDateTime::currentMSecsSinceEpoch()},{"data",31}}}};
  if(method=="GetMonitorDataSince") {
    QByteArray packet;const double values[]={52,31,38.4,3200,51,31,73.5,1900,4100,900};
    const auto now=QDateTime::currentMSecsSinceEpoch();
    for(quint8 id=0;id<10;++id) {
      const quint32 count=120;packet.append(reinterpret_cast<const char*>(&id),sizeof(id));packet.append(reinterpret_cast<const char*>(&count),sizeof(count));
      for(quint32 i=0;i<count;++i){const qint64 ts=now-300000+i*2500;double val=values[id]+std::sin(i*0.3+id)*((id==3)?900:(id==7)?180:2);packet.append(reinterpret_cast<const char*>(&ts),sizeof(ts));packet.append(reinterpret_cast<const char*>(&val),sizeof(val));}
    }
    return packet;
  }
  return std::nullopt;
}
