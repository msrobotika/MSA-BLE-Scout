"""Parser and no-action guard tests on the explicitly identified Scout board."""
import argparse,json,sys,time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from device import Device
VECTORS=[
 ("020106050954455354",True,{"name":"TEST","flags":6}),
 ("05ff4c000102",True,{"company_id":76}),
 ("03030f18",True,{"service_uuids16":[6159]}),
 ("1107ffeeddccbbaa99887766554433221100",True,{"service_uuid128":"00112233-4455-6677-8899-aabbccddeeff"}),
 ("0409410a42",True,{"name":"A?B"}),
 ("050941",False,{}),("020301",False,{}),("00"*32,False,{}),("00",True,{})]
def run(d):
 initial=d.command({"cmd":"status"});d.command({"cmd":"scan","enabled":False});time.sleep(.5)
 before=d.command({"cmd":"status"})
 try:
  assert not before["scanning"]
  for raw,valid,fields in VECTORS:
   item=d.command({"cmd":"parse_test","hex":raw});assert item["result"]=="parse_test" and item["valid"]==valid,(raw,item)
   for k,v in fields.items():assert item[k]==v,(k,item)
  assert d.command({"cmd":"parse_test","hex":"zz"})["result"]=="invalid_hex"
  assert d.command({"cmd":"connect","id":0})["result"]=="confirmation_required"
  assert d.command({"cmd":"read","index":0})["result"]=="confirmation_required"
  assert d.command({"cmd":"write","index":0,"value":"01","confirmed":True})["result"]=="unsupported_command"
  assert d.command({"cmd":"select","id":999})["result"]=="invalid_device"
  d.serial.write(b"x"*600+b"\n");after=d.command({"cmd":"status"});assert after["result"]=="status"
  assert after["connection_attempts"]==before["connection_attempts"] and after["read_attempts"]==before["read_attempts"]
  assert after["reports"]==before["reports"]
  return {"success":True,"parser_vectors":len(VECTORS),"malformed_hex_rejected":True,"missing_confirmation_rejected":True,"writes_unavailable":True,"invalid_selection_rejected":True,"console_recovers_after_overlong_line":True,"parse_tests_do_not_inject_radio_events":True,"guard_tests_did_not_connect_or_read":True,"timestamp":time.time()}
 finally:d.command({"cmd":"scan","enabled":initial["scan_enabled"]})
if __name__=="__main__":
 p=argparse.ArgumentParser(description=__doc__);p.add_argument("--port",default="COM4");p.add_argument("--mac",required=True);p.add_argument("--output",type=Path,required=True);a=p.parse_args();d=Device(a.port,a.mac)
 try:r=run(d);a.output.write_text(json.dumps(r,indent=2));print(json.dumps(r,indent=2))
 finally:d.close()
