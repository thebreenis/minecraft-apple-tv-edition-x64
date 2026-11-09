import sys, pathlib, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
import account_guard as g
from unittest.mock import patch, MagicMock
class AccountTests(unittest.TestCase):
 def test_missing_registry_gamertag(self):
  import winreg
  def value(key,name):
   if name=='Gamertag': raise FileNotFoundError(2,'missing registry value')
   return ({'AccountId':'same','PrimaryWebAccountId':'same','Xuid':'123'}[name],winreg.REG_SZ)
  with patch.object(winreg,'OpenKey',return_value=MagicMock()), patch.object(winreg,'QueryValueEx',side_effect=value), patch.object(g.diagnostics,'once'):
   a=g.windows_accounts()
  self.assertEqual(a.xbox_tag,'')
  self.assertEqual(g.decision(a,''),'reauth')
  self.assertEqual(g.decision(a,'123'),'ready')
 def test_friend_missing_tag_starts_auth(self):
  a=g.Accounts('same','123','','same','')
  self.assertEqual(g.decision(a,''),'reauth')
  self.assertEqual(g.decision(a,'123'),'ready')
  self.assertEqual(g.decision(a,'456'),'reauth')
  self.assertIn('Gamertag unavailable',g.describe(a))
  self.assertIn('Same account as Xbox app',g.describe(a))
  self.assertNotIn('could not be verified',g.describe(a))
 def test_mismatch_not_bypassed(self):
  self.assertEqual(g.decision(g.Accounts('one','123','','two',''),'123'),'windows_mismatch')
 def test_missing_ids_still_blocked(self):
  for a in [g.Accounts('','123','Name','same',''),g.Accounts('same','','Name','same',''),g.Accounts('same','123','Name','','')]:
   self.assertEqual(g.decision(a,'123'),'unverified')
 def test_existing_named_account(self):
  a=g.Accounts('same','123','Name','same','Name')
  self.assertEqual(g.decision(a,'123'),'ready')
  self.assertEqual(g.decision(a,''),'reauth')
 def test_display_name_not_identity(self):
  self.assertEqual(g.decision(g.Accounts('one','123','Same name','two','Same name'),'123'),'windows_mismatch')
unittest.main()
