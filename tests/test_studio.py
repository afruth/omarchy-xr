import copy
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
from backend import Manager, default_layout, validate

class FakeHypr:
    def __init__(self):
        self.outputs={"eDP-1":{"name":"eDP-1","width":1920,"height":1080,"x":0,"y":0,"scale":1}}
        self.fail=False
    def __call__(self,*args):
        if args==("-j","monitors"): return json.dumps(list(self.outputs.values()))
        if args[:2]==("output","create"):
            name=args[3]; self.outputs[name]={"name":name,"width":1920,"height":1080,"x":1920,"y":0,"scale":1};return "ok"
        if args[:2]==("output","remove"):
            del self.outputs[args[2]];return "ok"
        if args[0]=="eval":
            if self.fail: raise RuntimeError("Injected compositor failure")
            name,w,h,x,y=re.search(r'output="([^"]+)", mode="(\d+)x(\d+)@60", position="(-?\d+)x(-?\d+)"',args[1]).groups()
            self.outputs[name].update(width=int(w),height=int(h),x=int(x),y=int(y))
            return "ok"
        raise AssertionError(args)

class LayoutTests(unittest.TestCase):
    def test_validation(self):
        for change in (lambda x:x.update(fps=0),lambda x:x["monitors"][0].update(width=0),lambda x:x["monitors"][1].update(x=0),lambda x:x["monitors"][0].update(id='bad"name')):
            layout=default_layout();change(layout)
            with self.assertRaises(ValueError):validate(layout)
    def test_no_three_monitor_limit(self):
        layout=default_layout();layout["monitors"]=[{"id":str(i),"width":640,"height":480,"x":(i%10)*640,"y":(i//10)*480} for i in range(100)]
        validate(layout)
    def test_lifecycle(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr(); manager=Manager(temp,"/unused",fake)
            try:
                layout=default_layout();manager.apply(layout)
                self.assertEqual(len(manager.owned),3)
                self.assertEqual(fake.outputs[manager.prefix+"1"]["x"],2020)
                changed=copy.deepcopy(layout);changed["monitors"]=changed["monitors"][:2];changed["monitors"][0]["height"]=720
                manager.apply(changed)
                self.assertEqual(len(manager.owned),2)
                self.assertEqual(manager.load(),changed)
                self.assertEqual(len((Path(temp)/"viewer.tsv").read_text().splitlines()),2)
                manager.cleanup();self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:manager.lock.close()
    def test_failed_creation_is_cleaned(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake);fake.fail=True
            try:
                with self.assertRaises(RuntimeError):manager.apply(default_layout())
                self.assertFalse(manager.owned);self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:manager.lock.close()
    def test_stale_outputs_recovery(self):
        with tempfile.TemporaryDirectory() as temp:
            fake=FakeHypr();manager=Manager(temp,"/unused",fake);manager.apply(default_layout());manager.lock.close()
            recovered=Manager(temp,"/unused",fake)
            try:self.assertEqual(list(fake.outputs),["eDP-1"])
            finally:recovered.lock.close()

if __name__=="__main__":unittest.main()
