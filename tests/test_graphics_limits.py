import unittest
from pathlib import Path
import sys
from unittest.mock import Mock
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"studio"))
from graphics_limits import summarize, validate_dimensions
from backend import Manager, default_layout

class GraphicsLimitsTests(unittest.TestCase):
    def test_smallest_gpu_and_driver_constraints(self):
        gpu=lambda n:{"texture":n,"renderbuffer":n,"viewportWidth":n,"viewportHeight":n}
        limits=summarize({"gpus":[gpu(16384),gpu(4096)],"cards":[{"maxWidth":8192,"maxHeight":2048}]})
        self.assertEqual((limits["maxWidth"],limits["maxHeight"]),(4096,2048))
        self.assertTrue(limits["complete"])
        layout=default_layout();layout["monitors"][0]["height"]=2049
        with self.assertRaisesRegex(ValueError,"Monitor 1"):
            validate_dimensions(layout,limits)

    def test_unknown_is_not_claimed_as_hardware_detection(self):
        limits=summarize({"gpus":[{"error":"failed"}]})
        self.assertFalse(limits["detected"])
        self.assertEqual(limits["maxWidth"],8192)
        self.assertIsNone(limits["hardwareWidth"])

    def test_rejected_before_compositor_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            runner=Mock()
            manager=Manager(directory,"/unused",runner)
            manager.graphics_limits={"maxWidth":1024,"maxHeight":1024}
            try:
                with self.assertRaisesRegex(ValueError,"per-monitor limit"):
                    manager.apply(default_layout())
                runner.assert_not_called()
                self.assertFalse(manager.owned)
            finally:manager.lock.close()
