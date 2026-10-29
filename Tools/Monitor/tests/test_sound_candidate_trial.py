"""Trial output ownership must preserve earlier evidence."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('sound_trial_tool',Path(__file__).resolve().parents[1]/'sound_candidate_trial.py')
trial=importlib.util.module_from_spec(spec);spec.loader.exec_module(trial)

class OutputOwnership(unittest.TestCase):
    def test_each_existing_artifact_rejects_without_modification(self):
        for suffix in ('-tone.wav','-serial.bin','-serial.jsonl','-run.json','-analysis.json'):
            with self.subTest(suffix=suffix), tempfile.TemporaryDirectory() as directory:
                prefix=Path(directory)/'trial'
                saved=Path(str(prefix)+suffix);saved.write_bytes(b'original evidence')
                with self.assertRaises(FileExistsError):trial.check_output_paths(prefix)
                self.assertEqual(saved.read_bytes(),b'original evidence')
                self.assertEqual(list(Path(directory).iterdir()),[saved])
    def test_new_prefix_has_no_side_effects(self):
        with tempfile.TemporaryDirectory() as directory:
            trial.check_output_paths(Path(directory)/'new')
            self.assertEqual(list(Path(directory).iterdir()),[])
