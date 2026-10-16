"""Local USB provisioning and diagnostics; not required while the canary runs."""
import argparse
import json
import time
from pathlib import Path
import serial

class Device:
    def __init__(self, port="COM4", expected_mac=None):
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=.15, write_timeout=3)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        try:
            self.serial.open()
            self.serial.write(b"\n")
            self.state = self.command({"cmd": "status"}, timeout=8)
            if not self.state.get("firmware", "").startswith("MSA-BLE-Scout-"):
                raise RuntimeError("The target is not running MSA BLE Scout")
            if expected_mac and self.state.get("mac") != expected_mac.lower():
                raise RuntimeError("The board MAC does not match the selected device")
        except BaseException:
            self.serial.close()
            raise

    def _json(self, timeout=5):
        deadline = time.monotonic() + timeout
        line = bytearray()
        discard = False
        while time.monotonic() < deadline:
            for value in self.serial.read(1):
                if value == 10:
                    try:
                        item = None if discard else json.loads(line)
                    except (ValueError, UnicodeDecodeError):
                        item = None
                    line.clear()
                    discard = False
                    if isinstance(item, dict):
                        return item
                elif not discard:
                    if len(line) >= 16384:
                        line.clear()
                        discard = True
                    else:
                        line.append(value)
        raise TimeoutError("No valid response from MSA BLE Scout")

    def command(self, obj, timeout=5):
        self.serial.write(json.dumps(obj, separators=(",", ":")).encode() + b"\n")
        return self._json(timeout)

    def capture(self):
        from PIL import Image
        header = self.command({"cmd": "capture"})
        width, height, size = (header.get(k, 0) for k in ("width", "height", "bytes"))
        if header.get("result") != "capture_begin" or not (0 < width <= 170 and 0 < height <= 320 and size == width*height*2):
            raise RuntimeError("Invalid capture header")
        data = bytearray()
        deadline = time.monotonic() + 20
        while len(data) < size and time.monotonic() < deadline:
            data.extend(self.serial.read(min(8192, size-len(data))))
        if len(data) != size or self._json().get("result") != "capture_end":
            raise RuntimeError("Incomplete capture")
        rgb = bytearray()
        for i in range(0, size, 2):
            v = (data[i] << 8) | data[i+1]
            rgb.extend((((v >> 11) & 31)*255//31, ((v >> 5) & 63)*255//63, (v & 31)*255//31))
        return Image.frombytes("RGB", (width, height), bytes(rgb))

    def close(self):
        self.serial.close()

def records(d,command):
    expected=command+"_begin"
    first=d.command({"cmd":command})
    if first.get("result")!=expected: raise RuntimeError("Export did not start")
    result=[]
    deadline=time.monotonic()+20
    while len(result)<100 and time.monotonic()<deadline:
        item=d._json(min(5,max(.1,deadline-time.monotonic())))
        if item.get("result")==command+"_end": return result
        if item.get("record") in ("device","service","characteristic"):result.append(item)
        else: raise RuntimeError("Unexpected export record")
    raise RuntimeError("Export exceeded its bounds")

def main():
    p=argparse.ArgumentParser(description="MSA BLE Scout USB provisioning / evidence export")
    p.add_argument("--port",default="COM4")
    p.add_argument("--mac")
    sub=p.add_subparsers(dest="action",required=True)
    sub.add_parser("status")
    sub.add_parser("devices")
    sub.add_parser("select").add_argument("id",type=int)
    sub.add_parser("scan").add_argument("state",choices=("on","off"))
    sub.add_parser("disconnect")
    sub.add_parser("clear")
    for action in ("connect","read"):
        a=sub.add_parser(action)
        a.add_argument("index",type=int)
        a.add_argument("--confirm-owned-device",action="store_true",required=True)
    sub.add_parser("page").add_argument("index",type=int,choices=range(5))
    a=sub.add_parser("display");a.add_argument("profile",choices=("170x320","135x240"));a.add_argument("--invert",choices=("on","off"),default="on")
    sub.add_parser("capture").add_argument("path",type=Path)
    sub.add_parser("export").add_argument("path",type=Path)
    args=p.parse_args();d=Device(args.port,args.mac)
    try:
        if args.action=="devices": result=records(d,"devices")
        elif args.action=="export":
            result={"status":d.command({"cmd":"status"}),"records":records(d,"export")}
            args.path.write_text(json.dumps(result,indent=2),encoding="utf8")
            result={"saved":str(args.path)}
        elif args.action=="capture":d.capture().save(args.path);result={"saved":str(args.path)}
        else:
            obj={"cmd":args.action}
            if args.action=="select": obj["id"]=args.id
            elif args.action in ("connect","read"):
                obj["id" if args.action=="connect" else "index"]=args.index;obj["confirmed"]=True
            elif args.action=="scan":obj["enabled"]=args.state=="on"
            elif args.action=="page":obj["index"]=args.index
            elif args.action=="display":obj.update(profile=0 if args.profile=="170x320" else 1,invert=args.invert=="on")
            result=d.command(obj)
        print(json.dumps(result,indent=2))
    finally:d.close()

if __name__=="__main__":main()
