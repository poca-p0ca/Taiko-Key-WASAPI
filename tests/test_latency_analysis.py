"""Synthetic timestamp/onset fixtures. These are NOT measured app latency results."""
import csv
import importlib.util
from pathlib import Path
import tempfile
import unittest
import numpy as np

spec = importlib.util.spec_from_file_location('compare_latency', Path(__file__).resolve().parents[1]/'tools/compare_latency.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AnalysisTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def fixture(self, delay=.023, bad_flags=False, noise=False, gap=False):
        rate, seconds = 48000, 4
        pcm = np.zeros((rate*seconds, 2), dtype='<f4')
        for key in (1., 2., 3.):
            at = round((key+delay)*rate)
            pcm[at:at+480, :] = .4
        if noise:
            pcm[:] += .01
        pcm.tofile(self.folder/'stereo.f32')
        (self.folder/'metadata.txt').write_text(f'label=synthetic\nendpoint=fake\nsample_rate={rate}\nqpc_frequency=10000000\nframes={rate*seconds}\nperiod_before=480\nperiod_after=480\n')
        (self.folder/'keys.csv').write_text('sequence,qpc\n1,1010000000\n2,1020000000\n3,1030000000\n')
        with (self.folder/'packets.csv').open('w', newline='') as file:
            out = csv.writer(file)
            out.writerow(['first_frame', 'frames', 'device_position', 'qpc_100ns', 'arrival_qpc', 'flags'])
            for first in range(0, rate*seconds, 480):
                flags = 4 if bad_flags and first == rate else 0
                stamp = round((100+first/rate)*1e7)
                if gap and first >= rate:
                    stamp += 100000
                # Arrival is deliberately late; analysis MUST use packet timestamps instead.
                out.writerow([first,480,first,stamp,stamp+900000,flags])

    def test_known_onsets_ignore_arrival_delay(self):
        self.fixture()
        result = module.analyze(self.folder)
        self.assertEqual(result['stats']['n'], 3)
        self.assertAlmostEqual(result['stats']['median_ms'], 23, places=6)

    def test_bad_timestamp_excluded(self):
        self.fixture(bad_flags=True)
        result = module.analyze(self.folder)
        self.assertEqual(result['stats']['n'], 2)
        self.assertEqual(result['rejected']['capture_discontinuity_or_timestamp_error'], 1)

    def test_existing_audio_rejected(self):
        self.fixture(noise=True)
        result = module.analyze(self.folder)
        self.assertEqual(result['stats']['n'], 0)
        self.assertEqual(result['rejected']['preexisting_audio_or_tail'], 3)

    def test_capture_gap_excluded(self):
        self.fixture(gap=True)
        result = module.analyze(self.folder)
        self.assertEqual(result['stats']['n'], 2)

    def test_sample_quantization(self):
        self.fixture(delay=.023456)
        result = module.analyze(self.folder)
        self.assertLess(abs(result['stats']['median_ms']-23.456), 1000/48000)

    def test_tiny_packet_overlap_excludes_local_window(self):
        self.fixture()
        path = self.folder/'packets.csv'
        rows = list(csv.DictReader(path.read_text().splitlines()))
        rows[100]['qpc_100ns'] = str(int(rows[100]['qpc_100ns']) - 230)
        with path.open('w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        result = module.analyze(self.folder)
        self.assertEqual(result['timestamp_boundary_overlaps'], 1)
        self.assertEqual(result['stats']['n'], 2)
        self.assertAlmostEqual(result['stats']['median_ms'], 23, places=6)

    def test_large_clock_reversal_still_refused(self):
        self.fixture()
        path = self.folder/'packets.csv'
        rows = list(csv.DictReader(path.read_text().splitlines()))
        rows[100]['qpc_100ns'] = str(int(rows[100]['qpc_100ns']) - 10000)
        with path.open('w', newline='') as file:
            writer = csv.DictWriter(file, fieldnames=rows[0].keys())
            writer.writeheader()
            writer.writerows(rows)
        with self.assertRaisesRegex(ValueError, 'reversal exceeds one sample'):
            module.analyze(self.folder)


if __name__ == '__main__':
    unittest.main()
