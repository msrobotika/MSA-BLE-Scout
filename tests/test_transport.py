import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"tools"))
from device import Device

class Stream:
    def __init__(self, data):
        self.data = bytearray(data)
        self.closed = False
        self.writes = []
    def open(self): pass
    def close(self): self.closed = True
    def write(self, data): self.writes.append(data)
    def read(self, n):
        b = bytes(self.data[:n])
        del self.data[:n]
        return b

class TransportTests(unittest.TestCase):
    def device(self, data):
        d = Device.__new__(Device)
        d.serial = Stream(data)
        return d

    def test_noise_nonobjects_and_oversize_record_resynchronize(self):
        d = self.device(b"boot log\n[]\n" + b"x"*20000 + b'\n{"result":"status"}\n')
        self.assertEqual(d._json(), {"result": "status"})

    def test_utf8_split_into_bytes_is_preserved(self):
        d = self.device('{"name":"señuelo"}\n'.encode())
        self.assertEqual(d._json()["name"], "señuelo")

    def test_json_reader_does_not_consume_binary_capture(self):
        d = self.device(b'{"result":"capture_begin"}\n\xff\x00\n')
        self.assertEqual(d._json()["result"], "capture_begin")
        self.assertEqual(d.serial.read(3), b"\xff\x00\n")

    def test_wrong_target_closes_serial_before_mutating_commands(self):
        stream = Stream(json.dumps({"firmware":"MSA-BLE-Scout-0.1","mac":"aa:bb:cc:dd:ee:ff"}).encode()+b"\n")
        with patch("device.serial.Serial", return_value=stream):
            with self.assertRaises(RuntimeError): Device("COM4", "00:11:22:33:44:55")
        self.assertTrue(stream.closed)
        self.assertEqual(json.loads(stream.writes[-1]), {"cmd":"status"})

    def test_capture_rejects_out_of_bounds_size(self):
        d = self.device(b'{"result":"capture_begin","width":170,"height":320,"bytes":999999}\n')
        with self.assertRaises(RuntimeError): d.capture()

if __name__ == "__main__": unittest.main()
