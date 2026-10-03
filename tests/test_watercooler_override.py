"""Exercise production D-Bus handlers with a fake BLE worker, without hardware writes."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

def function(source, name):
    start = source.index('UccDBusInterfaceAdaptor::' + name + '(')
    start = source.rfind('\n', 0, start) + 1
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class WatercoolerOverride(unittest.TestCase):
    def test_manual_override_only_after_success_and_auto_can_be_restored(self):
        source = (ROOT/'uccd/src/UccDBusService.cpp').read_text()
        harness = r'''
#include <cassert>
#include <string>
struct PolkitAuthority { static constexpr const char* ACTION_CONTROL = "control"; };
struct Worker {
 bool succeeds=true; int fan=-1, pump=-1;
 bool setFanSpeed(int value) { if(succeeds) fan=value; return succeeds; }
 bool setPumpVoltage(int value) { if(succeeds) pump=value; return succeeds; }
};
struct Profile {
 std::string id="profile";
 struct { bool autoControlWC=true; std::string fanProfile="curve"; } fan;
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
 assert(!service.m_activeProfile.fan.autoControlWC && service.updates==1 && adaptor.notifications==1);
 assert(adaptor.SetWaterCoolerAutoControl(true));
 assert(service.m_activeProfile.fan.autoControlWC && service.updates==2);
 for(int voltage:{4,2,3,0}) {
  assert(adaptor.SetWaterCoolerPumpVoltage(voltage) && worker.pump==voltage);
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
