"""Exercise production D-Bus handlers with a fake BLE worker, without hardware writes."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

def function(source, name, owner="UccDBusInterfaceAdaptor"):

    start = source.index(owner + '::' + name + '(')
    start = source.rfind('\n', 0, start) + 1
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class WatercoolerOverride(unittest.TestCase):
    def test_saved_manual_values_restore_after_reconnect_and_retry(self):
        source = (ROOT/'uccd/src/UccDBusService.cpp').read_text()
        harness = r'''
#include <cassert>
struct UccProfile { struct {bool autoControlWC=false; int manualFanSpeed=70, manualPumpVoltage=3;} fan; };
struct Worker {
 int fan=-1,pump=-1,sends=0; bool succeeds=true;
 int getLastFanSpeed() const {return fan;}
 int getLastPumpVoltage() const {return pump;}
 bool setFanSpeed(int v) {++sends;if(succeeds) fan=v;return succeeds;}
 bool setPumpVoltage(int v) {++sends;if(succeeds) pump=v;return succeeds;}
};
struct UccDBusService {
 Worker *m_waterCoolerWorker;
 struct {struct {bool value=true;bool load() const{return value;}} waterCoolerConnected;} m_dbusData;
 void applyManualWaterCoolerSettings(const UccProfile&);
};
'''
        harness += function(source,'applyManualWaterCoolerSettings','UccDBusService')
        harness += r'''
int main() {
 Worker worker;UccDBusService service{&worker};UccProfile saved;
 service.applyManualWaterCoolerSettings(saved);assert(worker.fan==70 && worker.pump==3 && worker.sends==2);
 service.applyManualWaterCoolerSettings(saved);assert(worker.sends==2);
 worker.fan=10;worker.pump=-1;service.m_dbusData.waterCoolerConnected.value=false;
 service.applyManualWaterCoolerSettings(saved);assert(worker.sends==2);
 service.m_dbusData.waterCoolerConnected.value=true;worker.succeeds=false;
 service.applyManualWaterCoolerSettings(saved);assert(worker.fan==10 && worker.pump==-1);
 worker.succeeds=true;service.applyManualWaterCoolerSettings(saved);assert(worker.fan==70 && worker.pump==3);
 const int sent=worker.sends;saved.fan.autoControlWC=true;worker.fan=20;
 service.applyManualWaterCoolerSettings(saved);assert(worker.fan==20 && worker.sends==sent);
 saved.fan.autoControlWC=false;saved.fan.manualFanSpeed=-1;saved.fan.manualPumpVoltage=-1;
 service.applyManualWaterCoolerSettings(saved);assert(worker.sends==sent);
 saved.fan.manualFanSpeed=0;saved.fan.manualPumpVoltage=4;
 service.applyManualWaterCoolerSettings(saved);assert(worker.fan==0 && worker.pump==4);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp=pathlib.Path(directory)/'test.cpp';cpp.write_text(harness)
            binary=pathlib.Path(directory)/'test'
            subprocess.run(['c++','-std=c++20',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_manual_override_only_after_success_and_auto_can_be_restored(self):
        source = (ROOT/'uccd/src/UccDBusService.cpp').read_text()
        harness = r'''
#include <cassert>
#include <string>
struct PolkitAuthority { static constexpr const char* ACTION_CONTROL = "control"; };
struct Worker {
 bool succeeds=true; int fan=-1, pump=-1;
 int getLastFanSpeed() const { return fan; }
 int getLastPumpVoltage() const { return pump; }
 bool setFanSpeed(int value) { if(succeeds) fan=value; return succeeds; }
 bool setPumpVoltage(int value) { if(succeeds) pump=value; return succeeds; }
};
struct Profile {
 std::string id="profile";
 struct { bool autoControlWC=true; int manualFanSpeed=-1, manualPumpVoltage=-1; std::string fanProfile="curve"; } fan;
 struct { std::string keyboardProfileId="keyboard"; } keyboard;
};
struct Service {
 Worker* m_waterCoolerWorker; Profile m_activeProfile; int updates=0;
 void updateDBusActiveProfileData() { ++updates; }
};
struct UccDBusInterfaceAdaptor {
 Service* m_service; bool authorized=true; int notifications=0;
 bool checkAuth(const char*) { return authorized; }
 void emitProfileChanged(const std::string&,const std::string&,const std::string&) { ++notifications; }
 void updateWaterCoolerAutoControl(bool);
 bool SetWaterCoolerAutoControl(bool);
 bool SetWaterCoolerFanSpeed(int);
 bool SetWaterCoolerPumpVoltage(int);
};
'''
        for name in ('updateWaterCoolerAutoControl', 'SetWaterCoolerAutoControl',
                     'SetWaterCoolerFanSpeed', 'SetWaterCoolerPumpVoltage'):
            harness += function(source, name) + '\n'
        harness += r'''
int main() {
 Worker worker; Service service{&worker}; UccDBusInterfaceAdaptor adaptor{&service};
 assert(!adaptor.SetWaterCoolerFanSpeed(-1));
 assert(!adaptor.SetWaterCoolerPumpVoltage(1));
 assert(service.m_activeProfile.fan.autoControlWC && service.updates==0);
 worker.succeeds=false;
 assert(!adaptor.SetWaterCoolerFanSpeed(40));
 assert(service.m_activeProfile.fan.autoControlWC && service.updates==0);
 worker.succeeds=true;
 assert(adaptor.SetWaterCoolerFanSpeed(40) && worker.fan==40);
 assert(service.m_activeProfile.fan.manualFanSpeed==40);
 assert(!service.m_activeProfile.fan.autoControlWC && service.updates==1 && adaptor.notifications==1);
 assert(adaptor.SetWaterCoolerAutoControl(true));
 assert(service.m_activeProfile.fan.autoControlWC && service.updates==2);
 service.m_activeProfile.fan.manualPumpVoltage=3;worker.pump=-1;
 assert(adaptor.SetWaterCoolerFanSpeed(40));
 assert(service.m_activeProfile.fan.manualPumpVoltage==3);
 for(int voltage:{4,2,3,0}) {
  assert(adaptor.SetWaterCoolerPumpVoltage(voltage) && worker.pump==voltage);
  assert(service.m_activeProfile.fan.manualPumpVoltage==voltage);
  assert(!service.m_activeProfile.fan.autoControlWC);
  assert(adaptor.SetWaterCoolerAutoControl(true));
 }
 adaptor.authorized=false;
 assert(!adaptor.SetWaterCoolerAutoControl(false));
 assert(!adaptor.SetWaterCoolerFanSpeed(80));
 assert(service.m_activeProfile.fan.autoControlWC && worker.fan==40);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = pathlib.Path(directory)/'test.cpp'; cpp.write_text(harness)
            binary = pathlib.Path(directory)/'test'
            subprocess.run(['c++','-std=c++20',str(cpp),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__ == '__main__':
    unittest.main()
