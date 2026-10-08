"""Independent fixtures for matched-work rejection and display-rate reporting."""
from pathlib import Path
import tempfile
import unittest
from compare_perf_runs import compare


def record(second, time, packets, triangles, presents, displayed=None):
    fence = '' if displayed is None else f'VIPER WII GX FENCE site=4 calls={displayed} us=0\n'
    return (f'VIPER WII PROFILE guest={second}.000003 elapsed_us={time}\n'+fence+
            f'VIPER WII WORK packets={packets} triangles={triangles} presents={presents} clears=0\n')


class ComparisonTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup)
        self.a = Path(self.temp.name)/'a'; self.b = Path(self.temp.name)/'b'
        self.a.write_text(record(70, 100, 10, 20, 100, 100)+record(71, 2000100, 30, 50, 120, 120))
        self.b.write_text(record(70, 100, 10, 20, 100, 50)+record(71, 1000100, 30, 50, 120, 60))

    def test_matched_alternate(self):
        result = compare(self.a,self.b,70,71)
        self.assertEqual(result['time_reduction_percent'],50)
        self.assertEqual(result['baseline']['logical_fps'],10)
        self.assertEqual(result['candidate']['logical_fps'],20)
        self.assertEqual(result['candidate']['displayed_fps'],10)

    def test_headless_display_unknown(self):
        self.b.write_text(record(70,100,10,20,100)+record(71,1000100,30,50,120))
        self.assertIsNone(compare(self.a,self.b,70,71)['candidate']['displayed_fps'])

    def test_missing_work(self):
        self.b.write_text(record(70,100,10,20,100)+'VIPER WII PROFILE guest=71.0 elapsed_us=1000100\n')
        with self.assertRaisesRegex(ValueError,'incomplete'):compare(self.a,self.b,70,71)

    def test_mismatched_work(self):
        self.b.write_text(record(70,100,10,20,100)+record(71,1000100,31,50,120))
        with self.assertRaisesRegex(ValueError,'workload differs'):compare(self.a,self.b,70,71)

    def test_duplicate(self):
        with self.b.open('a') as file:file.write(record(71,1000100,30,50,120))
        with self.assertRaisesRegex(ValueError,'duplicate'):compare(self.a,self.b,70,71)

    def test_malformed_header(self):
        with self.b.open('a') as file:file.write('VIPER WII PROFILE guest=bad elapsed_us=1\n')
        with self.assertRaisesRegex(ValueError,'malformed profile'):compare(self.a,self.b,70,71)

    def test_record_order(self):
        self.b.write_text(record(71,1000100,30,50,120)+record(70,100,10,20,100))
        with self.assertRaisesRegex(ValueError,'out of order'):compare(self.a,self.b,70,71)

    def test_stale_tail(self):
        with self.b.open('a') as file:file.write('VIPER WII GX FENCE site=4 calls=999 us=0\n')
        self.assertEqual(compare(self.a,self.b,70,71)['candidate']['displayed_fps'],10)


if __name__ == '__main__':unittest.main()
