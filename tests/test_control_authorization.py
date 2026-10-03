"""Keep routine cooler/keyboard methods in the password-free control action."""
import pathlib
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[1]
ROUTINE = (
    'ApplyProfile', 'SaveCustomProfile', 'SetBatchStateMap', 'SetWaterCoolerAutoControl',
    'SetFanProfileCPU', 'SetFanProfileDGPU', 'ApplyFanProfiles', 'RevertFanProfiles',
    'SaveCustomFanProfile', 'DeleteCustomFanProfile',
    'SaveCustomKeyboardProfile', 'DeleteCustomKeyboardProfile',
    'SetKeyboardBacklightStatesJSON', 'SetFnLockStatus',
    'EnableWaterCooler', 'SetWaterCoolerFanSpeed', 'SetWaterCoolerPumpVoltage',
    'SetWaterCoolerLEDColor', 'TurnOffWaterCoolerLED',
    'TurnOffWaterCoolerFan', 'TurnOffWaterCoolerPump',
)

class ControlAuthorization(unittest.TestCase):
    def test_routine_methods_use_control_authorization(self):
        source = (ROOT/'uccd/src/UccDBusService.cpp').read_text()
        for method in ROUTINE:
            with self.subTest(method=method):
                body = re.search(r'UccDBusInterfaceAdaptor::' + method + r'\([^)]*\)\s*\{([^}]+)', source).group(1)
                self.assertIn('checkAuth( PolkitAuthority::ACTION_CONTROL )', body)
                self.assertNotIn('ACTION_MANAGE_HARDWARE', body)

    def test_policy_allows_active_control_without_prompt(self):
        policy = ET.parse(ROOT/'uccd/com.uniwill.uccd.policy')
        for action_id in ('read', 'control'):
            defaults = policy.find(f"action[@id='com.uniwill.uccd.{action_id}']/defaults")
            self.assertEqual(defaults.findtext('allow_active'), 'yes')
            self.assertEqual(defaults.findtext('allow_inactive'), 'no')
            self.assertEqual(defaults.findtext('allow_any'), 'no')
        defaults = policy.find("action[@id='com.uniwill.uccd.manage-hardware']/defaults")
        self.assertEqual(defaults.findtext('allow_active'), 'auth_admin_keep')

if __name__ == '__main__':
    unittest.main()
