"""Replay DMA frame/local-PTP interpolation using real C and a Fraction oracle.

This is an offline software check. Model intervals cover cursor quantization
under a linear-progress assumption, not acoustic or physical timing errors.
"""
import argparse
from dataclasses import dataclass, replace
from fractions import Fraction
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import sys

from audio_anchor_analysis import FIRMWARE, check_anchor_rows, parse_anchor_records
from audio_position_analysis import require

ROOT = Path(__file__).resolve().parents[1]
UINT64_MAX = 2**64-1
UINT32_MAX = 2**32-1
EVIDENCE_STATUS = "PASS_M322_LOCAL_PTP_DMA_ANCHORS_AND_ORIGINAL_SERVICES"
EXPECTED_UID = ("001A002D", "30365105", "36363631")
EMPIRICAL_MAX_ABS_NS = 100000
SOURCES = ("Analysis/audio_time_map_cli.c", "App/Audio/audio_time_map.c", "App/Audio/audio_time_map.h")


@dataclass(frozen=True)
class Anchor:
    valid: bool
    session: int
    frame_lo: int
    frame_hi: int
    sec: int
    nsec: int


@dataclass(frozen=True)
class Case:
    identifier: int
    left: Anchor
    right: Anchor
    target_session: int
    frame_position: int


def reference(left, right, target_session, frame_position):
    """Independent exact-rational closed-bin model and fixed status ordering."""
    def failure(status): return status, 0, 0, 0, 0
    if not left.valid or not right.valid:
        return failure(2)
    if any(a.nsec >= 10**9 or a.frame_lo > a.frame_hi or
           a.frame_hi-a.frame_lo > 4 for a in (left,right)):
        return failure(2)
    if not target_session or left.session != target_session or right.session != target_session:
        return failure(3)
    if any(a.frame_hi == UINT64_MAX for a in (left,right)):
        return failure(8)
    t0, t1 = left.sec*10**9+left.nsec, right.sec*10**9+right.nsec
    l0, u0 = left.frame_lo, left.frame_hi+1
    l1, u1 = right.frame_lo, right.frame_hi+1
    if t1 <= t0 or l1 <= u0:
        return failure(4)
    dt_ns = t1-t0
    if not 100000000 <= dt_ns <= 10000000000:
        return failure(5)
    if u1-l0 > 500000:
        return failure(8)
    if (Fraction((l1-u0)*10**9,dt_ns) < Fraction(47511160,1000) or
            Fraction((u1-l0)*10**9,dt_ns) > Fraction(48470982,1000)):
        return failure(6)
    if not u0 <= frame_position <= l1:
        return failure(7)
    lower_exact = Fraction(dt_ns*(frame_position-u0), u1-u0)
    upper_exact = Fraction(dt_ns*(frame_position-l0), l1-l0)
    lower_ns = t0+math.floor(lower_exact)
    upper_ns = t0+math.ceil(upper_exact)
    estimate_ns = (lower_ns+upper_ns)//2
    return 0, 1, estimate_ns, lower_ns, upper_ns


def sha256(data): return hashlib.sha256(data).hexdigest()


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, "duplicate JSON field: " + key)
        result[key] = value
    return result


def native_environment(compiler_or_executable):
    environment = dict(os.environ)
    environment["PATH"] = str(Path(compiler_or_executable).resolve().parent)+os.pathsep+environment.get("PATH", "")
    return environment


def build_native(output_dir):
    compiler = os.environ.get("CC") or shutil.which("gcc")
    require(compiler is not None and shutil.which(compiler) is not None,
            "Native C compiler missing; install GCC or set CC to its executable")
    compiler = shutil.which(compiler)
    executable = output_dir / ("audio_time_map_cli.exe" if os.name == "nt" else "audio_time_map_cli")
    command = [compiler,"-std=c11","-Wall","-Wextra","-Werror","-pedantic","-I",str(ROOT/"App"),
               str(ROOT/SOURCES[0]),str(ROOT/SOURCES[1]),"-o",str(executable)]
    try:
        built = subprocess.run(command,capture_output=True,text=True,timeout=30,
                               env=native_environment(compiler),check=False)
    except (OSError,subprocess.SubprocessError) as error:
        raise ValueError(f"Native compile failed: {error}") from error
    require(built.returncode == 0 and not built.stderr.strip(),
            f"Native compile failed ({built.returncode}): {built.stderr.strip()}")
    require(executable.is_file(),"Native compile did not produce executable")
    return dict(command=command,returncode=built.returncode,stdout=built.stdout,stderr=built.stderr,
                source_sha256={source:sha256((ROOT/source).read_bytes()) for source in SOURCES},
                executable_path=str(executable.resolve()),executable_sha256=sha256(executable.read_bytes()))


def encode_case(case):
    def fields(anchor):
        return [int(anchor.valid),anchor.session,anchor.frame_lo,anchor.frame_hi,anchor.sec,anchor.nsec]
    return " ".join(map(str,[case.identifier,*fields(case.left),*fields(case.right),
                             case.target_session,case.frame_position]))+"\n"


def execute_native(executable, data):
    # GCC's runtime DLLs may live next to the compiler, not next to this build.
    compiler=os.environ.get("CC") or shutil.which("gcc") or executable
    try:
        return subprocess.run([str(executable)],input=data,capture_output=True,text=True,
                              timeout=30,env=native_environment(compiler),check=False)
    except (OSError,subprocess.SubprocessError) as error:
        raise ValueError(f"Native runtime failed: {error}") from error


def decode_results(output, cases):
    lines=output.splitlines()
    require(len(lines) == len(cases),"Native output count mismatch")
    results=[]
    for line,case in zip(lines,cases):
        tokens=line.split()
        require(len(tokens) == 6 and all(re.fullmatch(r"[0-9]+",token) for token in tokens),
                "Malformed native output tokens")
        result=tuple(map(int,tokens))
        identifier,status,valid,estimate,lower,upper=result
        require(all(value <= UINT64_MAX for value in result) and identifier == case.identifier,
                "Native output ID/order/range mismatch")
        require(0 <= status <= 8 and valid in (0,1),"Native output status/valid mismatch")
        if status:
            require(result[2:] == (0,0,0,0),"Native failure did not clear all output fields")
        else:
            require(valid == 1 and lower <= estimate <= upper,"Native interval/order failure")
        results.append(result)
    return results


def run_native(executable,cases):
    require(bool(cases),"No native cases")
    completed=execute_native(executable,"".join(encode_case(case) for case in cases))
    require(completed.returncode == 0 and not completed.stderr.strip(),
            f"Native runtime failed ({completed.returncode}): {completed.stderr.strip()}")
    return decode_results(completed.stdout,cases)


def synthetic_cases(count=4000):
    """Fixed seed; translations include near-uint64 frame IDs and uint32 seconds."""
    require(count > 0,"Synthetic case count must be positive")
    generator=random.Random(0x323)
    cases=[]
    for index in range(count):
        dt_ns=generator.randint(120000000,9990000000)
        span=round(Fraction(dt_ns*47991071,10**12))
        width0,width1=generator.randrange(5),generator.randrange(5)
        base=(UINT64_MAX-span-100) if index%3 == 0 else generator.randrange(10**12)
        time0=(UINT32_MAX-11)*10**9+generator.randrange(10**9) if index%5 == 0 else generator.randrange(10**15)
        time1=time0+dt_ns
        session=generator.randint(1,UINT32_MAX)
        left=Anchor(True,session,base,base+width0,time0//10**9,time0%10**9)
        right=Anchor(True,session,base+span,base+span+width1,time1//10**9,time1%10**9)
        frame=generator.randint(left.frame_hi+1,right.frame_lo)
        target_session=session
        # One in four cases exercises rejection without overshadowing healthy cases.
        if index%4 == 3:
            variant=(index//4)%8
            if variant == 0: left=replace(left,valid=False)
            elif variant == 1: target_session=0
            elif variant == 2: right=replace(right,sec=left.sec,nsec=left.nsec)
            elif variant == 3: right=replace(right,sec=left.sec,nsec=left.nsec+1) if left.nsec < 999999999 else replace(right,sec=left.sec,nsec=left.nsec)
            elif variant == 4: right=replace(right,frame_lo=base+span*2,frame_hi=base+span*2+width1) if base+span*2+width1 < UINT64_MAX else replace(right,frame_lo=base+span//2,frame_hi=base+span//2+width1)
            elif variant == 5: frame=left.frame_lo
            elif variant == 6: left=replace(left,frame_hi=left.frame_lo+5)
            else: left=replace(left,nsec=10**9)
        cases.append(Case(index+1,left,right,target_session,frame))
    return cases


def load_bound_evidence(serial,evidence):
    try:
        raw=serial.read_bytes()
        evidence_raw=evidence.read_bytes()
        acceptance=json.loads(evidence_raw,object_pairs_hook=unique_object)
        records=[json.loads(line,object_pairs_hook=unique_object) for line in raw.decode("utf-8").splitlines()]
    except (OSError,ValueError,UnicodeError) as error:
        raise ValueError(f"Cannot read valid raw capture/evidence: {error}") from error
    require(isinstance(acceptance,dict) and acceptance.get("status") == EVIDENCE_STATUS and
            acceptance.get("hardware_verified") is True,"Original M3.2.2 acceptance is not PASS/hardware verified")
    raw_files=acceptance.get("raw_files")
    require(isinstance(raw_files,dict),"Missing raw_files capture hash binding")
    bound=[]
    for filename,digest in raw_files.items():
        require(isinstance(filename,str),"Malformed raw_files path")
        candidate=Path(filename.replace("\\",os.sep))
        if not candidate.is_absolute(): candidate=ROOT/candidate
        if candidate.resolve() == serial.resolve(): bound.append(digest)
    require(len(bound) == 1 and bound[0] == sha256(raw),"Raw capture SHA-256 binding mismatch")
    require(all(isinstance(record,dict) and "incomplete_tail_hex" not in record and
                isinstance(record.get("line"),str) for record in records),"Malformed/incomplete serial record")
    boot_indices=[i for i,record in enumerate(records) if record["line"].startswith("$BOOT,")]
    require(len(boot_indices) == 1,"Expected exactly one BOOT in raw capture")
    boot_index=boot_indices[0]
    boot=records[boot_index]["line"].rstrip("\r\n")
    fields={}
    for token in boot.split(",")[1:]:
        key,separator,value=token.partition("=")
        require(separator and key and value and key not in fields,"Malformed/duplicate BOOT identity field")
        fields[key]=value
    require(fields.get("FW") == FIRMWARE and
            tuple(fields.get(f"UID_W{i}") for i in range(3)) == EXPECTED_UID,
            "Unexpected firmware/BOOT UID")
    require(acceptance.get("boot") == boot,"BOOT identity differs from original acceptance")
    # The original raw bytes and pre-BOOT prefix are kept; only the checker view
    # starts at BOOT. All post-BOOT rows are checked before selecting holdouts.
    rows=parse_anchor_records(records[boot_index:])
    checked=check_anchor_rows(rows,minimum_valid=90)
    require(checked["valid"] >= 90,"Fewer than 90 valid anchors")
    return rows,dict(source=str(serial.resolve()),source_sha256=sha256(raw),
                     evidence=str(evidence.resolve()),evidence_sha256=sha256(evidence_raw),
                     boot=boot,uid=list(EXPECTED_UID),discarded_pre_boot_lines=boot_index,
                     pre_boot_records=records[:boot_index],original_anchor_check=checked)


def row_anchor(row):
    return Anchor(row["VALID"] == "1",int(row["SESSION"]),int(row["F_LO"]),int(row["F_HI"]),
                  int(row["SEC"]),int(row["NSEC"]))


def holdout_cases(rows):
    require(len(rows) >= 90 and all(row["STATUS"] == "OK" for row in rows[:90]),
            "Fixed first 90 anchor rows must all be valid")
    cases,held=[],[]
    for start in range(0,90,3):
        left,middle,right=rows[start:start+3]
        frame=(int(middle["F_LO"])+int(middle["F_HI"])+1)//2
        cases.append(Case(start//3+1,row_anchor(left),row_anchor(right),int(left["SESSION"]),frame))
        held.append(middle)
    return cases,held


def summarize_holdout(results,held):
    require(len(results) == len(held) and bool(held),"Holdout count mismatch")
    rows=[]
    for result,middle in zip(results,held):
        identifier,status,valid,estimate,lower,upper=result
        require(status == 0 and valid == 1,"Invalid native holdout estimate")
        actual_ns=int(middle["SEC"])*10**9+int(middle["NSEC"])
        residual_ns=actual_ns-estimate
        outside_ns=max(lower-actual_ns,actual_ns-upper,0)
        rows.append(dict(id=identifier,held_sequence=middle.get("SEQ"),observed_ns=actual_ns,
                         estimate_ns=estimate,model_lower_ns=lower,model_upper_ns=upper,
                         residual_ns=residual_ns,abs_residual_ns=abs(residual_ns),
                         covered=outside_ns == 0,outside_distance_ns=outside_ns))
    maximum=max(row["abs_residual_ns"] for row in rows)
    covered=sum(row["covered"] for row in rows)
    return rows,dict(count=len(rows),max_abs_residual_ns=maximum,
                     rms_residual_ns=math.sqrt(sum(row["residual_ns"]**2 for row in rows)/len(rows)),
                     signed_residual_min_ns=min(row["residual_ns"] for row in rows),
                     signed_residual_max_ns=max(row["residual_ns"] for row in rows),
                     covered_count=covered,outside_count=len(rows)-covered,
                     max_outside_distance_ns=max(row["outside_distance_ns"] for row in rows),
                     empirical_max_abs_limit_ns=EMPIRICAL_MAX_ABS_NS,
                     empirical_check_pass=maximum <= EMPIRICAL_MAX_ABS_NS)


def replay(serial,evidence,output_dir,synthetic_count=4000):
    require(not output_dir.exists(),"Output directory already exists; refusing overwrite")
    rows,binding=load_bound_evidence(serial,evidence)
    output_dir.mkdir(parents=True,exist_ok=False)
    build=build_native(output_dir)
    executable=Path(build["executable_path"])
    synthetic=synthetic_cases(synthetic_count)
    cases,held=holdout_cases(rows)
    synthetic_results=run_native(executable,synthetic)
    results=run_native(executable,cases)
    for group,actual in ((synthetic,synthetic_results),(cases,results)):
        for case,result in zip(group,actual):
            expected=(case.identifier,*reference(case.left,case.right,case.target_session,case.frame_position))
            require(result == expected,f"Real C/Fraction mismatch in case {case.identifier}: {result} != {expected}")
    detail,summary=summarize_holdout(results,held)
    for item,case in zip(detail,cases):
        item.update(left_sequence=(item["id"]-1)*3+1,right_sequence=(item["id"]-1)*3+3,
                    target_frame=case.frame_position)
    report=dict(status="PASS_M323_SOFTWARE_REPLAY" if summary["empirical_check_pass"] else "FAIL_M323_EMPIRICAL_CHECK",
                hardware_verified=False,**binding,native_build=build,synthetic_cases=len(synthetic),
                c_fraction_matches=len(synthetic)+len(cases),holdout=summary,
                analysis_script_sha256=sha256(Path(__file__).read_bytes()),
                holdout_policy="Fixed nonoverlap (1,2,3)..(88,89,90); fit outer only; integer phase midpoint ties down",
                scope="Software replay only; unsynchronized local PTP/continuous NDTR frame coordinate; linear-model cursor interval only; excludes physical/acoustic/codec/FIFO calibration and course accuracy acceptance")
    for filename,value in (("summary.json",report),("holdout.json",detail),("native-build.json",build)):
        with (output_dir/filename).open("x",encoding="utf-8") as stream:
            json.dump(value,stream,indent=2,ensure_ascii=False)
            stream.write("\n")
    for prefix,inputs,outputs in (("synthetic",synthetic,synthetic_results),
                                  ("holdout",cases,results)):
        with (output_dir/(prefix+"-input.txt")).open("x",encoding="ascii") as stream:
            stream.write("".join(encode_case(case) for case in inputs))
        with (output_dir/(prefix+"-output.txt")).open("x",encoding="ascii") as stream:
            stream.write("".join(" ".join(map(str,result))+"\n" for result in outputs))
    return report


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial",required=True,type=Path)
    parser.add_argument("--evidence",required=True,type=Path)
    parser.add_argument("--output-dir",required=True,type=Path,help="New directory; never overwrites an existing run")
    args=parser.parse_args(argv)
    try:
        report=replay(args.serial,args.evidence,args.output_dir)
    except (OSError,ValueError,KeyError,TypeError) as error:
        print(f"ERROR: {error}",file=sys.stderr)
        return 1
    print(json.dumps(dict(status=report["status"],hardware_verified=False,holdout=report["holdout"])))
    return int(not report["holdout"]["empirical_check_pass"])


if __name__ == "__main__": sys.exit(main())
